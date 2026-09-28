/*
 * extract_ipv4.c
 *
 * Extracts a single valid IPv4 address (optionally followed by :port)
 * embedded anywhere in a line of text, using only hand-written,
 * character-by-character parsing (no atoi/strtol/sscanf/inet_* / regex).
 *
 * See the accompanying explanation for scanning/validation reasoning
 * and edge cases.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LINE_BUF_SIZE 1024u

/* ------------------------------------------------------------------ */
/* Prototype (must match exactly, per requirements)                    */
/* ------------------------------------------------------------------ */
int extractIPv4(const char* str, unsigned long* outAddress, int* outPort);

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* Characters that are allowed to make up a "candidate token". */
static int isTokenChar(char c) {
    return isdigit((unsigned char)c) || c == '.' || c == ':';
}

/*
 * Attempts to parse the exact substring tok[0..len) as a full
 * "octet.octet.octet.octet[:port]" grammar match, with NO leftover
 * characters permitted. Returns 1 and fills the output params on
 * success; returns 0 (leaving outputs untouched) on any failure.
 *
 * All digit accumulation is done manually: val = val*10 + (c - '0').
 */
static int validateToken(const char* tok, int len,
                          unsigned long* outAddress, int* outPort) {
    int i = 0;
    unsigned int octets[4];
    int o;

    for (o = 0; o < 4; o++) {
        int start, digits;
        unsigned int val;

        /* Every octet after the first must be preceded by a literal '.' */
        if (o > 0) {
            if (i >= len || tok[i] != '.') {
                return 0;
            }
            i++;
        }

        start = i;
        digits = 0;
        val = 0;

        while (i < len && isdigit((unsigned char)tok[i])) {
            digits++;
            if (digits > 3) {
                /* More than 3 digits in an octet -> the WHOLE token is
                 * invalid; we do not truncate to salvage a shorter
                 * valid run inside it. */
                return 0;
            }
            val = val * 10u + (unsigned int)(tok[i] - '0');
            i++;
        }

        if (digits == 0) {
            return 0; /* empty octet, e.g. "..", leading '.', etc. */
        }
        if (val > 255u) {
            return 0; /* out of range */
        }
        if (digits > 1 && tok[start] == '0') {
            return 0; /* leading zero, e.g. "01", "00" */
        }

        octets[o] = val;
    }

    /* Optional ":port" */
    {
        int port = -1;

        if (i < len && tok[i] == ':') {
            int start, digits;
            unsigned long val;

            i++; /* consume ':' */
            start = i;
            digits = 0;
            val = 0;

            while (i < len && isdigit((unsigned char)tok[i])) {
                digits++;
                if (digits > 5) {
                    return 0; /* too many digits for a port */
                }
                val = val * 10u + (unsigned long)(tok[i] - '0');
                i++;
            }

            if (digits == 0) {
                return 0; /* colon present but no port digits follow */
            }
            if (val > 65525UL) {
                /* NOTE: the required range is 0-65525 (not the usual
                 * 65535) — implemented literally per the given grammar. */
                return 0;
            }
            if (digits > 1 && tok[start] == '0') {
                return 0; /* leading zero in port, e.g. ":007" */
            }

            port = (int)val;
        }

        /* The entire token must be consumed exactly. Any leftover
         * character here (stray '.', stray ':', a second colon, a
         * trailing/leading separator, etc.) invalidates the match. */
        if (i != len) {
            return 0;
        }

        *outAddress = ((unsigned long)octets[0] << 24) |
                      ((unsigned long)octets[1] << 16) |
                      ((unsigned long)octets[2] << 8)  |
                      ((unsigned long)octets[3]);
        *outPort = port;
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Returns 1 if a valid address was found, 0 otherwise.
 * On success: *outAddress holds the 32-bit value, and
 * *outPort holds the port number, or -1 if no port was present.
 * On failure: *outAddress is set to 0 and *outPort is set to -1.
 */
int extractIPv4(const char* str, unsigned long* outAddress, int* outPort) {
    int i = 0;

    *outAddress = 0;
    *outPort = -1;

    if (str == NULL) {
        return 0;
    }

    while (str[i] != '\0') {
        int start, len;
        unsigned long addr;
        int port;

        if (!isTokenChar(str[i])) {
            i++;
            continue;
        }

        /* Found the start of a maximal candidate run. */
        start = i;
        while (str[i] != '\0' && isTokenChar(str[i])) {
            i++;
        }
        len = i - start;

        if (validateToken(str + start, len, &addr, &port)) {
            *outAddress = addr;
            *outPort = port;
            return 1;
        }

        /* Token was invalid in full; do not try to salvage a
         * substring of it. Resume scanning right after it (i is
         * already positioned there). */
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* main()                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    char line[LINE_BUF_SIZE];

    for (;;) {
        size_t len;
        int hadNewline;

        printf("Enter a line of text (or END to quit): ");
        fflush(stdout);

        if (fgets(line, (int)sizeof(line), stdin) == NULL) {
            /* EOF or read error on stdin */
            printf("\nProgram terminated.\n");
            break;
        }

        len = strlen(line);
        hadNewline = (len > 0 && line[len - 1] == '\n');

        if (hadNewline) {
            line[len - 1] = '\0';
            len--;
        } else {
            /* The line was longer than our buffer could hold in one
             * fgets() call. Drain the remainder of the actual input
             * line from stdin so it doesn't corrupt the next prompt. */
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
                /* discard */
            }
        }

        /* Tolerate CRLF-style input (trailing '\r' left after '\n' strip). */
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }

        if (strcmp(line, "END") == 0) {
            printf("Program terminated.\n");
            break;
        }

        {
            unsigned long addr;
            int port;

            if (extractIPv4(line, &addr, &port)) {
                unsigned int a = (unsigned int)((addr >> 24) & 0xFFUL);
                unsigned int b = (unsigned int)((addr >> 16) & 0xFFUL);
                unsigned int c = (unsigned int)((addr >> 8)  & 0xFFUL);
                unsigned int d = (unsigned int)(addr & 0xFFUL);

                if (port == -1) {
                    printf("Extracted IPV4 address: %u.%u.%u.%u "
                           "(decimal value: %lu, port: none)\n",
                           a, b, c, d, addr);
                } else {
                    printf("Extracted IPV4 address: %u.%u.%u.%u "
                           "(decimal value: %lu, port: %d)\n",
                           a, b, c, d, addr, port);
                }
            } else {
                printf("Invalid input: no valid IPv4 address found\n");
            }
        }
    }

    return 0;
}
