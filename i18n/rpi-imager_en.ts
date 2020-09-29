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
        <location filename="../downloadthread.cpp" line="127"/>
        <source>Error running diskpart: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="148"/>
        <source>Error removing existing partitions</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="174"/>
        <source>Authentication cancelled</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="177"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="178"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="199"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="213"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="225"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="346"/>
        <source>Access denied error while writing file to disk.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="351"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="357"/>
        <source>Error writing file to disk</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="585"/>
        <source>Error writing to storage (while flushing)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="592"/>
        <source>Error writing to storage (while fsync)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="573"/>
        <source>Download corrupt. Hash does not match</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="626"/>
        <source>Error writing first block (partition table)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="682"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="701"/>
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
        <location filename="../main.qml" line="23"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="90"/>
        <location filename="../main.qml" line="303"/>
        <source>Operating System</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="102"/>
        <source>CHOOSE OS</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="120"/>
        <source>Select this button to change the operating system</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="133"/>
        <location filename="../main.qml" line="587"/>
        <source>SD Card</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="145"/>
        <location filename="../main.qml" line="860"/>
        <source>CHOOSE SD CARD</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="158"/>
        <source>Select this button to change the destination SD card</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="175"/>
        <source>WRITE</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>Select this button to start writing the image</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="220"/>
        <source>CANCEL WRITE</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="223"/>
        <location filename="../main.qml" line="802"/>
        <source>Cancelling...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="235"/>
        <source>CANCEL VERIFY</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="238"/>
        <location filename="../main.qml" line="825"/>
        <location filename="../main.qml" line="878"/>
        <source>Finalizing...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="384"/>
        <location filename="../main.qml" line="854"/>
        <source>Erase</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="385"/>
        <source>Format card as FAT32</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="392"/>
        <source>Use custom</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="393"/>
        <source>Select a custom .img from your computer</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="416"/>
        <source>Back</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="417"/>
        <source>Go back to main menu</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="474"/>
        <source>Released: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="477"/>
        <source>Cached on your computer</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="479"/>
        <source>Local file</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="481"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="638"/>
        <location filename="../main.qml" line="690"/>
        <location filename="../main.qml" line="696"/>
        <source>Mounted as %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="692"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="734"/>
        <source>Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="735"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="746"/>
        <source>Warning</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="752"/>
        <location filename="../main.qml" line="805"/>
        <source>Writing... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="765"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="784"/>
        <source>Error downloading OS list from Internet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="828"/>
        <source>Verifying... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="846"/>
        <source>Error</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="853"/>
        <source>Write Successful</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="855"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="857"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="885"/>
        <location filename="../main.qml" line="927"/>
        <source>Error parsing os_list.json</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="965"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="980"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
