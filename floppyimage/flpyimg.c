/* Reads and writes DOS floppy disk images.
Use to back up things like bootable disks.

Read from drive to file:
  flpyimg switches... driveletter: file

Write from file to drive:
  flpyimg switches... file driveletter:

    -b  boot sector only
    -h  help
    -v  verify

This program only works as a DOS program, and hence will
only run on Windows XP and earlier.
It could probably be easily ported as long as dos_abs_disk_read/write functions
are available.
To compile, use the Digital Mars C compiler.
*/

/* Some background info:
For the old 160K formats (or anything under about 512KB), just get DEBUG.
For 160KB, type the following (do not type what's after the semicolon on the
line, and don't type any lines not starting with 4 spaces):
    l 0100 0 0 136        ; l(oad to) 0100(h from drive) 0(h starting at
sector) 0(h and going for) 136(h sectors)
    r cx 8000                ; r(egister) cx( =) 8000(h : low part of byte
count is 8000h)
    r bx 0002                ; r(egister) bx( =) 0002(h - high part of byte
count is 0002h - 00028000h bytes)
    n c:\drive_a.img       ; n(ame the file) c:\drive_a.img (- use a
different name if you want.)
    w                            ; w(rite the file)
    q                             ; q(uit)

For 180KB, change the 136 to 168, the 8000 to D000, and don't change the
0002
For 320KB, change the 136 to 26C, the 8000 to 0000, and the 0002 to 0005
For 360KB, change the 136 to 2D0, the 8000 to A000, and the 0002 to 0005

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <assert.h>

typedef unsigned char byte;

#pragma pack(1)
struct BootSector
{
    byte jmp[3];        // jmp instruction
    byte oemname[8];    // OEM name & version
    unsigned short bytesPerSector;
    byte sectorsPerAllocationUnit;
    unsigned short reservedSectors;     // starting at 0
    byte numberFATs;
    unsigned short numRoots;
    unsigned short logicalSectors;
    byte media;
    unsigned short sectorsPerFAT;
    unsigned short sectorsPerTrack;
    unsigned short numHeads;
    unsigned short numHiddenSectors;
};
#pragma pack()

struct Params
{
    int drive;          // drive (0 = A:, 1 = B:, etc.)
    char *file;         // file name
    int read;           // !=0 read, 0 write
    int verify;         // !=0 verify
    int bootsector;     // !=0 do boot sector only
};

struct Params params;

void floppyimage_read();
void floppyimage_verify();
void floppyimage_write();

int main(int argc, char *argv[])
{
    int i;

    if (argc <= 1)
        goto Lusage;

    params.drive = -1;
    for (i = 1; i < argc; i++)
    {
        char *p = argv[i];

        if (strcmp(p, "-v") == 0)
            params.verify = 1;
        else if (strcmp(p, "-b") == 0)
            params.bootsector = 1;
        else if (strcmp(p, "-h") == 0)
            goto Lusage;
        else if (p[0] && p[1] == ':')
        {
            if (params.drive >= 0)
                goto Lerr;
            if (!params.file)
                params.read = 1;
            if (*p >= 'A' && *p <= 'Z')
                params.drive = *p - 'A';
            else if (*p >= 'a' && *p <= 'z')
                params.drive = *p - 'a';
            else
                goto Lerr;
        }
        else
        {
            if (params.file)
                goto Lerr;
            params.file = p;
        }
    }
    if (params.drive < 0 || !params.file)
        goto Lerr;

    if (params.verify)
        floppyimage_verify();
    else if (params.read)
        floppyimage_read();
    else
        floppyimage_write();

    return EXIT_SUCCESS;

Lerr:
    printf("Command line error\n");
    return EXIT_FAILURE;

Lusage:
        printf(
            "FLPYIMG: 1.00 Read/write floppy disk images.\n"
            "Copyright (c) 2003 by Digital Mars, All Rights Reserved\n"
            "www.digitalmars.com\n"
            "\n"
            "Read:\n"
            "  flpyimg switches... driveletter: file\n"
            "\n"
            "Write:\n"
            "  flpyimg switches... file driveletter:\n"
            "\n"
            "    -b  boot sector only\n"
            "    -h  help\n"
            "    -v  verify\n");
    return EXIT_SUCCESS;
}


void floppyimage_read()
{
    FILE *fp;
    char buffer[512];
    int errors = 0;
    struct BootSector *pbs;

    //printf("%x\n", sizeof(struct BootSector));
    assert(sizeof(struct BootSector) == 0x1E);

    fp = fopen(params.file, "wb");
    if (!fp)
    {
        printf("Error opening file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    long start_sec = 0;
    long logicalSectors;
    do
    {
        int r;
        int num_sec = 1;

        printf("Reading sector %d\r", start_sec);
        memset(buffer, 0, sizeof(buffer));
        r = dos_abs_disk_read(params.drive, num_sec, start_sec, buffer);
        if (r != 0)
        {
            printf("Error 0x%04x reading sector %ld\n", r, start_sec);
            errors++;
        }

        if (start_sec == 0)
        {   pbs = (struct BootSector *)buffer;
            printf("Logical sectors = %d\n", pbs->logicalSectors);
            logicalSectors = pbs->logicalSectors;
            if (logicalSectors == 0)
                logicalSectors = 310;   // assume 160Kb disk
        }

        if (fwrite(buffer, 1, sizeof(buffer), fp) < sizeof(buffer))
        {
            printf("Error writing to file '%s'\n", params.file);
            exit(EXIT_FAILURE);
        }

        start_sec++;
        if (params.bootsector)
            break;
    } while (start_sec < logicalSectors);

    if (fclose(fp) == -1)
    {
        printf("Error closing file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    if (errors)
    {   printf("Disk read with %d bad sectors\n", errors);
        exit(EXIT_FAILURE);
    }
    printf("Disk read successfully\n");
}

void floppyimage_verify()
{
    FILE *fp;
    char dbuffer[512];
    char fbuffer[512];
    int errors = 0;
    struct BootSector *pbs;
    long fsize;

    //printf("%x\n", sizeof(struct BootSector));
    assert(sizeof(struct BootSector) == 0x1E);

    fsize = filesize(params.file);
    if (fsize == -1L)
    {
        printf("Error opening file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    if (params.bootsector && fsize != 512)
    {
        printf("Boot sector size is 512, not %ld\n", fsize);
        exit(EXIT_FAILURE);
    }


    fp = fopen(params.file, "rb");
    if (!fp)
    {
        printf("Error opening file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    long start_sec = 0;
    long logicalSectors;
    do
    {
        int r;
        int num_sec = 1;

        printf("Verifying sector %d\r", start_sec);
        memset(dbuffer, 0, sizeof(dbuffer));
        r = dos_abs_disk_read(params.drive, num_sec, start_sec, dbuffer);
        if (r != 0)
        {
            printf("Error 0x%04x reading sector %ld\n", r, start_sec);
            errors = 1;
        }

        if (start_sec == 0)
        {   pbs = (struct BootSector *)dbuffer;
            printf("Logical sectors = %d\n", pbs->logicalSectors);
            logicalSectors = pbs->logicalSectors;
            if (logicalSectors == 0)
                logicalSectors = 310;   // assume 160Kb disk
            if (!params.bootsector && fsize != ((long)logicalSectors) * 512)
            {
                printf("File size is %ld but disk size is %ld\n", fsize, ((long) logicalSectors) * 512);
                exit(EXIT_FAILURE);
            }
        }

        if (fread(fbuffer, 1, sizeof(fbuffer), fp) < sizeof(fbuffer))
        {
            printf("Error reading from file '%s'\n", params.file);
            exit(EXIT_FAILURE);
        }

        if (memcmp(fbuffer, dbuffer, sizeof(fbuffer)) != 0)
        {
            printf("Sector %ld does not match\n", start_sec);
            errors++;
        }

        start_sec++;
        if (params.bootsector)
            break;
    } while (start_sec < logicalSectors);

    if (fclose(fp) == -1)
    {
        printf("Error closing file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    if (errors)
    {   printf("Disk verified with bad sectors\n");
        exit(EXIT_FAILURE);
    }

    printf("Disk verify succeeded\n");
}

void floppyimage_write()
{
    FILE *fp;
    char buffer[512];
    int errors = 0;
    struct BootSector *pbs;
    long fsize;

    //printf("%x\n", sizeof(struct BootSector));
    assert(sizeof(struct BootSector) == 0x1E);

    if (params.drive != 0 && params.drive != 1)
    {
        printf("Can only write to drive A: or B:\n");
        exit(EXIT_FAILURE);
    }

    fsize = filesize(params.file);
    if (fsize == -1L)
    {
        printf("Error opening file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    if (fsize & 511)
    {
        printf("File size must be multiple of sector size (512), not %ld\n", fsize);
        exit(EXIT_FAILURE);
    }


    fp = fopen(params.file, "rb");
    if (!fp)
    {
        printf("Error opening file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    long start_sec = 0;
    long logicalSectors = fsize / 512;
    do
    {
        int r;
        int num_sec = 1;

        if (fread(buffer, 1, sizeof(buffer), fp) < sizeof(buffer))
        {
            printf("Error reading from file '%s'\n", params.file);
            exit(EXIT_FAILURE);
        }

        printf("Writing sector %d\r", start_sec);
        r = dos_abs_disk_write(params.drive, num_sec, start_sec, buffer);
        if (r != 0)
        {
            printf("Error 0x%04x writing sector %ld\n", r, start_sec);
            errors++;
        }

        start_sec++;
        if (params.bootsector)
            break;
    } while (start_sec < logicalSectors);

    if (fclose(fp) == -1)
    {
        printf("Error closing file '%s'\n", params.file);
        exit(EXIT_FAILURE);
    }

    if (errors)
    {   printf("Disk written with %d bad sectors\n", errors);
        exit(EXIT_FAILURE);
    }

    printf("Disk write succeeded\n");
}

