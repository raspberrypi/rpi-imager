<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="tr_TR">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="166"/>
        <source>Error writing to storage</source>
        <translation>Depolama birimine yazma hatası</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="188"/>
        <location filename="../downloadextractthread.cpp" line="348"/>
        <source>Error extracting archive: %1</source>
        <translation>Arşiv çıkarılırken hata oluştu: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="234"/>
        <source>Error mounting FAT32 partition</source>
        <translation>FAT32 bölümü bağlanırken hata oluştu</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="244"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>İşletim sistemi FAT32 bölümünü bağlamadı</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="267"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Dizin değiştirirken hata oluştu &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="126"/>
        <source>Error running diskpart: %1</source>
        <translation>Diskpart çalıştırılırken hata oluştu: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error removing existing partitions</source>
        <translation>Mevcut bölümler kaldırılırken hata oluştu </translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="174"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>&apos;%1&apos; disk aygıtına erişmek için authopen çalıştırılırken hata oluştu</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="193"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Depolama cihazı açılamıyor &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="207"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>MBR sıfırlanırken yazma hatası</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="219"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation>Kartın son kısmını sıfırlamaya çalışırken yazma hatası. Kart yanlış kapasitenin tanımını yapıyor olabilir (olası sahte bölüm boyutu tanımı)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="537"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Depolama alanına yazma hatası (flushing sırasında)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="543"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Depoya yazma hatası (fsync sırasında)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="560"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>İndirme bozuk. Hash eşleşmiyor</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="590"/>
        <source>Error writing first block (partition table)</source>
        <translation>İlk bloğu yazma hatası (bölüm tablosu)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="642"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation>Depolamadan okuma hatası.
SD kart arızalı olabilir.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="661"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Yazma doğrulanamadı. SD kartın içeriği, üzerine yazılandan farklı.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Bölümleme hatası: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>fat32format başlatılırken hata oluştu</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>fat32format çalıştırılırken hata oluştu: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Yeni sürücü harfi belirlenirken hata oluştu</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Geçersiz cihaz: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Hatalı biçimlendirme (udisks2 aracılığıyla)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>sfdisk başlatılırken hata oluştu</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="196"/>
        <source>Error starting mkfs.fat</source>
        <translation>mkfs.fat başlatılırken hata oluştu</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="206"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>mkfs.fat çalıştırılırken hata oluştu: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="213"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Bu platform için biçimlendirme uygulanmadı</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="170"/>
        <source>Storage capacity is not large enough.
Needs to be at least %1 GB</source>
        <translation>Depolama kapasitesi yeterince büyük değil. 
En az %1 GB olması gerekiyor</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="176"/>
        <source>Input file is not a valid disk image.
File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation type="unfinished">Giriş dosyası geçerli bir disk görüntüsü değil. 
%1 bayt dosya boyutu 512 baytın katı değil.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="200"/>
        <source>Downloading and writing image</source>
        <translation>Görüntü indirme ve yazma</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="403"/>
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
        <location filename="../MsgPopup.qml" line="108"/>
        <source>YES</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
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
        <location filename="../main.qml" line="300"/>
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
        <location filename="../main.qml" line="133"/>
        <location filename="../main.qml" line="575"/>
        <source>SD Card</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="145"/>
        <location filename="../main.qml" line="859"/>
        <source>CHOOSE SD CARD</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="159"/>
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
        <location filename="../main.qml" line="219"/>
        <source>CANCEL WRITE</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="222"/>
        <location filename="../main.qml" line="801"/>
        <source>Cancelling...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="233"/>
        <source>CANCEL VERIFY</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="236"/>
        <location filename="../main.qml" line="824"/>
        <location filename="../main.qml" line="877"/>
        <source>Finalizing...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="373"/>
        <location filename="../main.qml" line="853"/>
        <source>Erase</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="374"/>
        <source>Format card as FAT32</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="381"/>
        <source>Use custom</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="382"/>
        <source>Select a custom .img from your computer</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="405"/>
        <source>Back</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="406"/>
        <source>Go back to main menu</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="463"/>
        <source>Released: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="466"/>
        <source>Cached on your computer</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="468"/>
        <source>Local file</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="470"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="628"/>
        <location filename="../main.qml" line="676"/>
        <source>Mounted as %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="719"/>
        <source>Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="720"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="731"/>
        <source>Warning</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="737"/>
        <location filename="../main.qml" line="804"/>
        <source>Writing... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="750"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="783"/>
        <source>Error downloading OS list from Internet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="827"/>
        <source>Verifying... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="845"/>
        <source>Error</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="852"/>
        <source>Write Successful</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="854"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="856"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="884"/>
        <location filename="../main.qml" line="926"/>
        <source>Error parsing os_list.json</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="964"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
