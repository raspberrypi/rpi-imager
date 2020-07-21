<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="166"/>
        <source>Error writing to storage</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="188"/>
        <location filename="../downloadextractthread.cpp" line="348"/>
        <source>Error extracting archive: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="234"/>
        <source>Error mounting FAT32 partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="244"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="267"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="131"/>
        <source>Error running diskpart: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="152"/>
        <source>Error removing existing partitions</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="178"/>
        <source>Authentication cancelled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="181"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="182"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="203"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="229"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="350"/>
        <source>Access denied error while writing file to disk.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="355"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="361"/>
        <source>Error writing file to disk</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="589"/>
        <source>Error writing to storage (while flushing)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="596"/>
        <source>Error writing to storage (while fsync)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="577"/>
        <source>Download corrupt. Hash does not match</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="630"/>
        <source>Error writing first block (partition table)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="705"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="196"/>
        <source>Error starting mkfs.fat</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="206"/>
        <source>Error running mkfs.fat: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="213"/>
        <source>Formatting not implemented for this platform</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="170"/>
        <source>Storage capacity is not large enough.
Needs to be at least %1 GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="176"/>
        <source>Input file is not a valid disk image.
File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="200"/>
        <source>Downloading and writing image</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="409"/>
        <source>Select image</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="38"/>
        <source>Error opening image file</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="96"/>
        <source>NO</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="122"/>
        <source>CONTINUE</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="91"/>
        <location filename="../main.qml" line="304"/>
        <source>Operating System</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="103"/>
        <source>CHOOSE OS</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="121"/>
        <source>Select this button to change the operating system</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="134"/>
        <location filename="../main.qml" line="588"/>
        <source>SD Card</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="146"/>
        <location filename="../main.qml" line="868"/>
        <source>CHOOSE SD CARD</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="159"/>
        <source>Select this button to change the destination SD card</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="176"/>
        <source>WRITE</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="181"/>
        <source>Select this button to start writing the image</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>CANCEL WRITE</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="224"/>
        <location filename="../main.qml" line="810"/>
        <source>Cancelling...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="236"/>
        <source>CANCEL VERIFY</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="239"/>
        <location filename="../main.qml" line="833"/>
        <location filename="../main.qml" line="886"/>
        <source>Finalizing...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="385"/>
        <location filename="../main.qml" line="862"/>
        <source>Erase</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="386"/>
        <source>Format card as FAT32</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="393"/>
        <source>Use custom</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="394"/>
        <source>Select a custom .img from your computer</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="417"/>
        <source>Back</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="418"/>
        <source>Go back to main menu</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="475"/>
        <source>Released: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="478"/>
        <source>Cached on your computer</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="480"/>
        <source>Local file</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="482"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="639"/>
        <location filename="../main.qml" line="691"/>
        <location filename="../main.qml" line="697"/>
        <source>Mounted as %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="693"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="735"/>
        <source>Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="736"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="747"/>
        <source>Warning</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="753"/>
        <location filename="../main.qml" line="813"/>
        <source>Writing... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="792"/>
        <source>Error downloading OS list from Internet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="836"/>
        <source>Verifying... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="854"/>
        <source>Error</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="861"/>
        <source>Write Successful</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="865"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="893"/>
        <location filename="../main.qml" line="935"/>
        <source>Error parsing os_list.json</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="973"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="988"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
