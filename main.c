#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#define SIZE 512

/*
Partition Structure 16 bytes
[status | chsStart   | type | chsEnd     | lba         | numSectors ]
[00     | [00,00,00] | 00   | [00,00,00] | 00 00 00 00 | 00 00 00 00]
*/
typedef struct mbrPartitionTable{
    uint8_t status;
    uint8_t chsStart[3];
    uint8_t type;
    uint8_t chsEnd[3];
    uint32_t lba;
    uint32_t numSectors;
}partition;

/*
MBR Structure
[Code      | Disk Signature | nulls     | Partition Table | Signature]
[440 bytes | 4 bytes        | 2 bytes   | 64 bytes/16[4]  | 2 bytes  ]

disregarding disk signature and nulls for convenience
*/
typedef struct mbr{
    uint8_t code[446];
    partition partitionTable[4]; 
    uint16_t signature;
}mbr;


/*
    
*/
uint32_t printMFT(int fd){
    // **********************mbr**********************
    mbr readMBR;

    // code contains first 446 bytes
    read(fd, readMBR.code, 446);

    // read from byte 454 which is the LBA of p0
    lseek(fd, 454, SEEK_SET);
    read(fd, &readMBR.partitionTable[0].lba, 4);


    // LBA*512 =  p0 address
    int partitionAddress = readMBR.partitionTable[0].lba * SIZE;

    
    // **********************mbr**********************


    // **********************vbr**********************
    uint32_t mftAddress;
    uint16_t ntfsSignature;

    // move to partition p0 (0x00100000)
    lseek(fd, partitionAddress, SEEK_SET);

    // check for its ntfs signature
    lseek(fd, 510+partitionAddress, SEEK_SET);
    read(fd, &ntfsSignature, 2);
    if(ntfsSignature != 0xAA55){
        printf("NTFS signature missing from partition: %X", ntfsSignature);
        return 0;
    }
    

    // read from (0x00104030) to get MFT address
    lseek(fd, 48+partitionAddress, SEEK_SET);
    read(fd, &mftAddress, 4);

    // multiply by 1000(hex) or 4096(dec)
    mftAddress *= 0x1000;

    return mftAddress+partitionAddress;
    // **********************vbr**********************
}


/*
    calculate offset from mftStart and go there
        EX: inode = 72 *1024 = 73728 = 0x12000 +0x104000 = 0x116000
    read 1k into buffer (0x400)
    print attribute value-name pairs
*/
void travEntry(int fd, int fileName[], int fileData[]){
    uint8_t buffer[400];
    read(fd, buffer, 400);

    // buffer[20] holds the offset to first attribute
    int attOffset = buffer[20];
    char* attName = "None";
    
    
    while(buffer[attOffset] != 0xFF && attOffset <= 400 && buffer[attOffset+4] != 0) {
        // switch for matching attribute descriptors to its values
        // insert position and length of filename attribute when coming accross it
        switch (buffer[attOffset])
        {
        case 0x10:
            attName = "$STANDARD_INFORMATION";
            break;
        case 0x20:
            attName = "$ATTRIBUTE_LIST";
            break;
        case 0x30:
            attName = "$FILE_NAME";
            fileName[0] = attOffset;
            fileName[1] = buffer[attOffset + 4];
            break;
        case 0x50:
            attName = "$SECURITY_DESCRIPTOR";
            break;
        case 0x60:
            attName = "$VOLUME_NAME";
            break;
        case 0x70:
            attName = "$VOLUME_INFORMATION";
            break;
        case 0x80:
            attName = "$DATA";
            fileData[0] = attOffset;
            fileData[1] = buffer[attOffset+4];
            break;
        case 0x90:
            attName = "$INDEX_ROOT";
            break;
        case 0xA0:
            attName = "$INDEX_ALLOCATION";
            break;
        case 0xB0:
            attName = "$BITMAP";
            break;
        case 0xD0:
            attName = "$EA_INFORMATION";
            break;
        case 0xE0:
            attName = "$EA";
            break;
        }

        //printf("Offset: %d\tValue: 0x%X\tLength: %X\tName: %s\n",attOffset, buffer[attOffset], buffer[attOffset+4], attName);
        attOffset += buffer[attOffset+4];
    }

    // print the use/deleted and directory/file flag
    printf("Status flag: %X\t", buffer[22]);
    switch (buffer[22])
    {
        case 0:
            printf("Deleted File\n");
            break;
        case 1:
            printf("In use File\n");
            break;
        case 2:
            printf("Deleted Directory\n");
            break;
        case 3:
            printf("In use Directory\n");
            break;

    }
}

/*
    read in the $FILE_NAME attribute and print:
        Parent Entry Number
        the file name in unicode
*/
void fileName(int fd, int fileAtt[], int x, char *name){
    uint8_t buffer[fileAtt[1]];
    read(fd, buffer, fileAtt[1]);

    // get the 6 byte parent entry at offset 24
    uint64_t parentEntry;
    lseek(fd, x+24, SEEK_SET);
    read(fd, &parentEntry, 8);
    parentEntry = parentEntry << 48;

    // 1 byte filename len value at offset 88 (24 + 0x40)
    printf("Filename: ");
    // use len to read the filename
    char fileName[buffer[88]];
    
    for(int i=0;i<buffer[88];i++){
        // get every other byte and print that character out
        fileName[i] = buffer[90+(2*i)];
    }
    printf("%s\n", fileName);
    strncat(name, fileName, buffer[88]+1);
}

/*
    from $DATA write the data to recovery file 
    
    if data is nonresident 
        navigate to datarun offsets and write data to file 
*/
void fileDatarun(int rfd, int fd, int data[], int x){
    uint8_t extra[data[1]];

    // get the 6 byte parent entry at offset 24
    uint8_t nonresidentFlag, datarun, clusterNums, clusterOffset;
    uint64_t dataSize;
    int run, flag = 1;

    lseek(fd, x+8, SEEK_SET);
    read(fd, &nonresidentFlag, 1);

    // if the file is a non-resident
    // get the cluster numbers and calculate offset of datarun
    if(nonresidentFlag){

        int loc = lseek(fd, x+56, SEEK_SET);
        read(fd, &dataSize, 8);
        printf("\nFlag: Non-Resident, 0x%X\nThe file size: %ld Bytes\n\n", nonresidentFlag, dataSize);
        //printf("*****\t%X\t*****\n", loc);
        
        x = lseek(fd, x+64, SEEK_SET);
        read(fd, &datarun, 1);
        // loop until get 00 instead of cluster 
        while(flag){

            clusterOffset = datarun/16;
            clusterNums = datarun%16;
            uint32_t* offset = (uint32_t*)malloc(clusterOffset*sizeof(uint32_t));
            uint32_t* nums = (uint32_t*)malloc(clusterNums*sizeof(uint32_t));
            //printf("\nCluster Offset size: %X\tCluster Nums: %X\n", clusterOffset, clusterNums);
            
            // read num of contiguous clusters
            lseek(fd, x+1, SEEK_SET);
            read(fd, nums, clusterNums);
            printf("Number of contiguous clusters: %d\n", *nums);
        
            // read offset to datarun
            lseek(fd, x+1+clusterNums, SEEK_SET);
            read(fd, offset, clusterOffset);
            
            //get msb to detirmine of offset is negative
            // int msb = *offset << 1;
            // printf("%X\n", msb);

            printf("Offset to cluster: 0x%.6X\n", *offset);


            // lseek to datarun and save position
            run = lseek(fd, *offset*0x1000, SEEK_SET);

            printf("Data Run is from offset 0x%X to 0x%X\n\n",run, ((*nums*4096))+(run) );
            // read based on amount of data left 
            // read the size of cluster
            int i=0;
            do{

                uint8_t cluster[4096];
                read(fd, &cluster, 4096);
                if(dataSize <= 4096){
                    printf("data ending: %ld\t%d\n", dataSize, i);
                    write(rfd, cluster, dataSize);
                }
                else{
                    write(rfd, cluster, 4096);
                }
                dataSize -= 4096;
                i++;
            }while(i < *nums);

                printf("*****\tend\t*****\n");
            // if last cluster containing data
            // else{
            //     printf("Data Run is from offset 0x%X to 0x%lX\n\n",run, (dataSize)+(run));
            //     uint8_t cluster[dataSize];
            //     read(fd, &cluster, (dataSize));
            //     write(rfd, cluster, dataSize);

            // }

            x = lseek(fd, x+1+clusterNums+clusterOffset, SEEK_SET);
            read(fd, &datarun, 1);
            clusterOffset = datarun/16;
            clusterNums = datarun%16;
            if(datarun == 0x00){
                flag = 0;
            }
            
        }
    }

    // if the file is resident
    // just get the file and print its contents
    else if(!nonresidentFlag){
        printf("\nFlag: Resident, 0x%X", nonresidentFlag);
        

        lseek(fd, x+16, SEEK_SET);
        read(fd, &dataSize, 4);

        lseek(fd,x+24, SEEK_SET);
        read(fd, &extra, dataSize);
        printf("\nSize: %ld Bytes\n", dataSize);

        printf("\nResident Data:\n*****\tStart\t*****\n");
        for(int i=0; i<dataSize;i++){
            printf("%C", extra[i]);
        }
        printf("*****\tend\t*****\n");
    }
}

/*
    read device and error checking
    if successful call printMFT(fd) to get to start of MFT
    lseek to the start of the MFT and from there lseek to given entry #
        entry # * 0x400 (1000 bytes) = offset from MFT
    traverse the entry by attribute and store the positions of FILE_NAME and DATA
        print the status flag
    go to FILE_NAME and get the parent entry and the filename

*/
int main(int argc, char** argv){
    // check number of arguements given
    if (argc != 3){
        printf("Incorrect Usage: %s\nExpected: /dev/sd<x> <entryNum>", argv[1]);
    }
    int entry = atoi(argv[2]);

    // opens the specified device with rw permissions
    int fd = open(argv[1], O_RDWR);
    if (fd < 0){
        printf("failed to open: %s\n", argv[1]);
    }

    // call printMFT to get address
    uint32_t mftStart = printMFT(fd);


    // move to mft start and from there to entry 
    int mftLocation = lseek(fd, mftStart, SEEK_SET);
    int offset = entry * 0x400;
    int entryLocation = lseek(fd, mftLocation+offset, SEEK_SET);
    printf("Entry %d address: 0x%X \n", entry, entryLocation);
    

    // retrieve the position of the $FILE_NAME and $DATA attributes 
    // 0 - position 1 - attribute size
    int fileAtt[2];
    int fileData[2];
    travEntry(fd, fileAtt, fileData);


    // move to $FILE_NAME and parse the wanted information
    char name[] = "/home/nihal/Documents/program/Recovered/R-";
    int nameLocation = lseek(fd, entryLocation+fileAtt[0], SEEK_SET);
    fileName(fd, fileAtt, nameLocation, name);



    // create and open file using name from FILE_NAME
    int rfd = open(name, O_CREAT|O_RDWR);
    fchmod(rfd, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH|S_IWOTH|S_IXOTH);

    // move to $DATA and parse the wanted information
    int dataLocation = lseek(fd, entryLocation+fileData[0], SEEK_SET);
    fileDatarun(rfd , fd, fileData, dataLocation);

    close(fd);
    close(rfd);
    return 0;
}
