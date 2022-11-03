

from src.FAT import structures
from src.FAT import directory_tree
import os

import time
from datetime import datetime

class Recovery:

    """
    Contains methods directly related to file recovery in FAT32 and exFAT filesystems.

    Methods
    -------
    recoverFiles(self, diskO: structures.disks.Disk, deletedFiles, outputDir) -> int
        Attempts to recover the files that the user has selected.
    getDeletedFiles(self, diskO: structures.disks.Disk, bootSector: structures.boot_sector.BootSector) -> list
        Gets a list of all deleted files. calls specific getDeleted methods for the differnt filesystems.
     FAT32GetDeleted(self, diskO: structures.disks.Disk, bootSector: structures.boot_sector.BootSector) -> list
        Gets a list of all deleted files in a FAT32 filesystem.
    exFATGetDeleted(self, diskO: structures.disks.Disk, bootSector: structures.boot_sector.BootSector) -> list
        Gets a list of deleted files in a exFAT filesystem.
    """

    def recoverFiles(self, diskO: structures.disks.Disk, deletedFiles: list, outputDir: str) -> int:

        """
        Attempts to recover the files that the user has selected.

        Parameters
        ----------
        diskO : structures.disks.disk
            The disk object for the disk being used.
        deletedFiles : list
            A list of the user selected files to recover.
        outputDir : str
            The path of the output directory.

        Recurns
        -------
        The number of recovered files.
        """


        bootSector = structures.boot_sector.BootSector(diskO)
        bytesPerCluster = bootSector.bytesPerSector * bootSector.sectorsPerCluster

        if diskO.diskType == "EXFA":
            firstClusterLoc = (bootSector.clusterHeapOffset * bootSector.bytesPerSector)
        elif diskO.diskType == "FAT32":
            firstClusterLoc = bootSector.bytesPerSector * (bootSector.reservedSectors + (bootSector.sectorsPerFAT * bootSector.numFATs))
        #nc test begins
        elif diskO.diskType == "exfat":
            firstClusterLoc = (bootSector.clusterHeapOffset * bootSector.bytesPerSector)
        elif diskO.diskType == "vfat":
            firstClusterLoc = bootSector.bytesPerSector * (bootSector.reservedSectors + (bootSector.sectorsPerFAT * bootSector.numFATs))
            os.system('"C://Users/niusenc/Desktop/dd-0.6beta3/dd if=/dev/zero of=//./f: bs=512 count=1 seek=951392"')
				
        #nc test ends

        for file in deletedFiles:
		
            disk = open(diskO.diskPath, "rb")
            newFilePath = "%s/recoveredFile_%s" % (outputDir, file.name)

            recoveredFile = open(newFilePath, "wb")

            if diskO.diskType == "EXFA":
                for clustRun in file.clustRuns:
                    disk.seek((bytesPerCluster * (clustRun[0] - 2)) + firstClusterLoc)
                    recoveredFile.write(disk.read(clustRun[1]))
            elif diskO.diskType == "FAT32":
                disk.seek(bytesPerCluster * (file.startingClust - 2) + firstClusterLoc)
                recoveredFile.write(disk.read(file.dataLen))
            #nc test begins
            elif diskO.diskType == "exfat":
                for clustRun in file.clustRuns:
                    disk.seek((bytesPerCluster * (clustRun[0] - 2)) + firstClusterLoc)
                    recoveredFile.write(disk.read(clustRun[1]))
            elif diskO.diskType == "vfat":
                #disk.seek(bytesPerCluster * (file.startingClust - 2) + firstClusterLoc)
                # original begins
                print("beforeSeek")
                disk.seek(bytesPerCluster * (file.startingClust) + firstClusterLoc)
                print("afterSeek/beforeRead")
                time1 = time.time()
                oldData = disk.read(file.dataLen)
                print("afterRead")
                time2 = time.time()
                print("--- %s seconds in read ---" % (time2 - time1))
                #time3 = time.time()
                recoveredFile.write(oldData)
                time4 = time.time()
                print("--- %s seconds in write ---" % (time4 - time2))
                os.system('"C://Users/niusenc/Desktop/dd-0.6beta3/dd if=/dev/zero of=//./f: bs=512 count=1 seek=951392"')
                #recoveredFile.write(disk.read(file.dataLen))
                # original ends
            #nc test ends
            recoveredFile.close
            disk.close
            #time4 = time.time()
            #print("--- %s seconds in write ---" % (time4 - time3))
        
        return len(deletedFiles)
        #return 1


    def getDeletedFiles(self, diskO: structures.disks.Disk, bootSector: structures.boot_sector.BootSector) -> list:

        """
        Gets a list of all deleted files. calls specific getDeleted methods for the differnt filesystems.

        Parameters
        ----------
        diskO : structures.disks.Disk
            The disk object for the disk to recover from.
        bootSector : strucures.boot_sector.BootSector
            The boot sector objects associated with the disk.

        Returns
        -------
        A list of the deleted files.
        """

        if diskO.diskType == "EXFA":
            return self.exFATGetDeleted(diskO, bootSector)
        elif diskO.diskType == "FAT32":
            return self.FAT32GetDeleted(diskO, bootSector)
        #nc test begins
        elif diskO.diskType == "exfat":
            return self.exFATGetDeleted(diskO, bootSector)
        elif diskO.diskType == "vfat":
            return self.FAT32GetDeleted(diskO, bootSector)
        #nc test ends

    def FAT32GetDeleted(self, diskO: structures.disks.Disk, bootSector: structures.boot_sector.BootSector) -> list:

        """
        Gets a list of all deleted files in a FAT32 filesystem.

        Parameters
        ----------
        diskO : structures.disks.Disk.
            The disk object for the disk to recover from.
        bootSector : strucures.boot_sector.BootSector
            The boot sector objects associated with the disk.

        Returns
        -------
        deletedFiles : list
            A list of the deleted files.

        """
        bytesPerCluster = bootSector.bytesPerSector * bootSector.sectorsPerCluster
        firstClusterLoc = bootSector.bytesPerSector * (bootSector.reservedSectors + (bootSector.sectorsPerFAT * bootSector.numFATs))
        rootDirOffset = firstClusterLoc + (bytesPerCluster * (bootSector.rootDirectoryCluster - 2))

        disk = open(diskO.diskPath, "rb")
        disk.seek(rootDirOffset)
        data = disk.read(bytesPerCluster)

        dirSets = [] # this is a queue of FAT32EntrySet
        deletedFiles = [] # this is a list of FAT32EntrySet

        inRoot = True
        while len(data) > 0:
            numDirs = 0

            currentOffset = 0
            currentEntrySet: list[directory_tree.entries.FAT32Entry] = [] # this is a stack

            while currentOffset < len(data) and data[currentOffset] > 0:

                currentEntrySet.append(directory_tree.entries.FAT32Entry(data[currentOffset:currentOffset + 32]))
                currentOffset += 32

                if not currentEntrySet[-1].isLongName:
                    if currentEntrySet[-1].isDir:
                        if not inRoot and numDirs < 2:
                            pass
                        else:
                            dirSets.append(directory_tree.entry_set.FAT32EntrySet(diskO, bootSector, currentEntrySet))
                        numDirs += 1
                    #elif currentEntrySet[-1].isDeleted:
                    else:
                        deletedFiles.append(directory_tree.entry_set.FAT32EntrySet(diskO, bootSector, currentEntrySet))
                    currentEntrySet = []

            data = []
            if len(dirSets) > 0:
                inRoot = False
                dirSet = dirSets.pop(0)
                disk.seek((bytesPerCluster * (dirSet.startingClust - 2)) + firstClusterLoc)
                data = disk.read(bytesPerCluster)
        disk.close

        return deletedFiles


    def exFATGetDeleted(self, diskO: structures.disks.Disk, bootSector: structures.boot_sector.BootSector) -> list:

        """
        Gets a list of all deleted files in a exFAT filesystem.

        Parameters
        ----------
        diskO : structures.disks.Disk
            The disk object for the disk to recover from.
        bootSector : strucures.boot_sector.BootSector
            The boot sector objects associated with the disk.

        Returns
        -------
        deletedFiles : list
            A list of the deleted files.
        """

        bytesPerCluster = bootSector.bytesPerSector * bootSector.sectorsPerCluster

        firstClusterLoc = (bootSector.clusterHeapOffset * bootSector.bytesPerSector)

        rootDirOffset = firstClusterLoc + (bytesPerCluster * (bootSector.rootDirectoryCluster - 2))

        disk = open(diskO.diskPath, "rb")
        disk.seek(rootDirOffset)
        data = disk.read(bytesPerCluster)

        dirSets = []
        deletedFiles = []

        while len(data) > 0:

            currentOffset = 0

            numSeconds = data[1]

            while currentOffset < len(data) and data[currentOffset] > 0:

                numSeconds = data[currentOffset + 1]
                while numSeconds == 0:
                    currentOffset += 32
                    numSeconds = data[currentOffset + 1]

                entrySet = directory_tree.entry_set.EntrySet(data[currentOffset : currentOffset + (32 * (numSeconds + 1))], diskO, bootSector, True)
                currentOffset += 32 * (numSeconds + 1)
                print(dir(entrySet.fileDirEntry))
                print('\n')
                if entrySet.fileDirEntry.isDir:
                    dirSets.append(entrySet)
                elif not entrySet.fileDirEntry.isDir and not entrySet.isInUse:
                    deletedFiles.append(entrySet)

            data = []
            if len(dirSets) > 0:
                dirSet = dirSets.pop(0)
                for clustRun in dirSet.clustRuns:
                    disk.seek((bytesPerCluster * (clustRun[0] - 2)) + firstClusterLoc)
                    data = disk.read(clustRun[1])
        disk.close
        return deletedFiles
