#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>

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

struct SectorClusterValues{
    uint32_t mftAddress;
    uint16_t bps;
    uint8_t spc;
    uint64_t totSec;
    int clusterSize;
    int numClusters;
};

/*
    
*/
struct SectorClusterValues printMFT(int fd){
    // **********************mbr**********************
    mbr readMBR;
    struct SectorClusterValues sc;
    // code contains first 446 bytes
    read(fd, readMBR.code, 446);

    // read from byte 454 which is the LBA of p0
    lseek(fd, 454, SEEK_SET);
    read(fd, &readMBR.partitionTable[0].lba, 4);


    // LBA*512 =  p0 address
    int partitionAddress = readMBR.partitionTable[0].lba * 512;
    
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
        return sc;
    }
    

    lseek(fd, 11+partitionAddress, SEEK_SET);
    read(fd, &sc.bps, 2);
    lseek(fd, 13+partitionAddress, SEEK_SET);
    read(fd, &sc.spc, 1);
    lseek(fd, 40+partitionAddress, SEEK_SET);
    read(fd, &sc.totSec, 8);
    sc.totSec += 1;
    sc.clusterSize = sc.bps*sc.spc;
    sc.numClusters = sc.totSec/sc.clusterSize;

    // read from (0x00104030) to get MFT address
    lseek(fd, 48+partitionAddress, SEEK_SET);
    read(fd, &mftAddress, 4);

    // multiply by 1000(hex) or 4096(dec)
    mftAddress *= 0x1000;
    sc.mftAddress = mftAddress+partitionAddress;
    return sc;
    // **********************vbr**********************
}

void getFiles(int fd, struct SectorClusterValues sc){
    // the sector start would have the local file header signature
    // the EOCD would be deeper in the cluster it is specific to file
    uint32_t sector[sc.bps/4];

    int x=0;
    for(int i=1;i<sc.totSec;i++){
        x = lseek(fd,x+sc.bps, SEEK_SET );
        read(fd, &sector, sc.bps);
        if(sector[0] == 0x04034b50){
            printf(".xlsx file found at sector %d 0x%X\n", i, x);
        }
    }
    

}

int main(int argc, char** argv){
    // check number of arguements given
    if (argc != 2){
        printf("Incorrect Usage: %s\nExpected: /dev/sd<x>", argv[1]);
    }

    // opens the specified device with rw permissions
    int fd = open(argv[1], O_RDWR);
    if (fd < 0){
        printf("failed to open: %s\n", argv[1]);
    }

    // call printMFT to get address
    // move to mft start and from there to entry 
    struct SectorClusterValues sc = printMFT(fd);
    //printf("BPS: %d   SPC: %d   Sectors: %ld\nSize: %d   Clusters: %d\n", sc.bps, sc.spc, sc.totSec, sc.clusterSize, sc.numClusters);
    getFiles(fd, sc);
    //int mftLocation = lseek(fd, mftStart, SEEK_SET);
    //printf("mftstart: %X\n", mftStart);
    
    return 0;
}