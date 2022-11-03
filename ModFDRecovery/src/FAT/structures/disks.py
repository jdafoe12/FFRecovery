

import os.path
import re
import time

class Disk:

    """
    Contains data associated with a disk.
    
    Attributes
    ----------
    diskPath : str
        The file path of the disk.
    diskType : str
        The disk type (ext4, ext3, ext2).
    """

    def __init__(self, diskPath: str, diskType: str):

        """
        Parameters
        ----------
        diskPath : str
            The path of the disk
        diskType : str
            The filesystem type of the disk.
        """

        self.diskPath = diskPath
        self.diskType = diskType

    def __str__(self):
        return self.diskPath


def getDisks()-> list:

    disks = []

    dl = "ABCDEFGHIJKLMNIPQRSTUVWXYZ"
    drives = ["%s" % d for d in dl if os.path.exists("%s:" % d)]

    for drive in drives:
        disk = open("\\\\.\\" + drive + ":", "rb")
        typeID = disk.read(7)

        typeID = typeID[3:7].decode()


        if typeID != "EXFA" and typeID != "NTFS" and typeID != "MSDO":
            disk.seek(0x52)
            typeID = disk.read(5)
            typeID = typeID.decode()



        if typeID == "MSDO":
            typeID = "vfat"
        disks.append(Disk("\\\\.\\" + drive + ":", typeID))

    i = 0
    while len(disks) > i:
        if disks[i].diskType != "EXFA" and disks[i].diskType != "FAT32" and disks[i].diskType != "vfat":
            disks.pop(i)
        else:
            i += 1

    return disks


# #nc test begins
# def getDisks() -> list:

#     """
#     Gets all valid disks and puts them in a list

#     Returns
#     -------
#     goodDisks : list[Disk]
#         A list of all valid disks
#     """
#     # /proc/mounts contains data on all mounted disk partitions
#     start_time = time.time()
#     mounts = open("/proc/mounts", "r")
#     disks: list[str] = mounts.readlines()

#     goodDisks: list[Disk] = []

#     for disk in disks:
#         # The different data fields in /proc/mounts are seperated by " " or ","
#         disk: str = re.split(" |,", disk)

#         if disk[0][0:4] == "/dev" and (disk[2] == "exfat" or disk[2] == "vfat") and disk[3] == "rw":
#             goodDisks.append(Disk(disk[0], disk[2]))
#     end_time = time.time()
#     print("--- %s seconds in disks ---" % (end_time - start_time))


#     return goodDisks
# #nc test ends