<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="nl_NL">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="166"/>
        <source>Error writing to storage</source>
        <translation>Fout bij schrijven naar opslag</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="188"/>
        <location filename="../downloadextractthread.cpp" line="348"/>
        <source>Error extracting archive: %1</source>
        <translation>Fout bij uitpakken archiefbestand: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="234"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Fout bij mounten FAT32 partitie</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="244"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Besturingssysteem heeft FAT32 partitie niet gemount</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="267"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Fout bij openen map &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="126"/>
        <source>Error running diskpart: %1</source>
        <translation>Fout bij uitvoeren diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error removing existing partitions</source>
        <translation>Fout bij verwijderen bestaande partities</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="174"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fout bij uitvoeren authopen: &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="192"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Fout bij openen opslagapparaat &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="207"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Fout bij wissen MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="219"/>
        <source>Write error while trying to zero out last part of card.
Card could be advertising wrong capacity (possible counterfeit)</source>
        <translation>Fout bij wissen laatste deel van de SD kaart.
Kaart geeft mogelijk onjuiste capaciteit aan (mogelijk counterfeit)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="523"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fout bij schrijven naar opslag (tijdens flushen)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="529"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fout bij schrijven naar opslag (tijdens fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="546"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Download corrupt. Hash komt niet overeen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="576"/>
        <source>Error writing first block (partition table)</source>
        <translation>Fout bij schrijven naar eerste deel van kaart (partitie tabel)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="622"/>
        <source>Error reading from storage.
SD card may be broken.</source>
        <translation>Fout bij lezen van SD kaart. Kaart is mogelijk defect.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="641"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verificatie mislukt. De gegevens die op de SD kaart staan wijken af van wat er naar geschreven is.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="181"/>
        <source>Error partitioning: %1</source>
        <translation>Fout bij partitioneren: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Fout bij starten fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Fout bij uitvoeren fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Fout bij vaststellen nieuwe letter van schijfstation</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Ongeldig device: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="144"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Fout bij formatteren (via udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="170"/>
        <source>Error starting sfdisk</source>
        <translation>Fout bij starten sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="192"/>
        <source>Error starting mkfs.fat</source>
        <translation>Fout bij starten mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="202"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Fout bij uitvoeren mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="209"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatteren is niet geimplementeerd op dit besturingssysteem</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="152"/>
        <source>Storage capacity is not large enough.
Needs to be at least %1 GB</source>
        <translation>Opslagcapaciteit niet groot genoeg.
Deze dient minimaal %1 GB te zijn</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="158"/>
        <source>Input file is not a valid disk image.
File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Invoerbestand is geen disk image.
Bestandsgrootte %1 bytes is geen veelvoud van 512 bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="178"/>
        <source>Downloading and writing image</source>
        <translation>Downloaden en schrijven van image</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="380"/>
        <source>Select image</source>
        <translation>Selecteer image</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="23"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="32"/>
        <source>Are you sure you want to quit?</source>
        <translation>Weet u zeker dat u wilt afsluiten?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="33"/>
        <source>Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Imager is nog niet klaar.&lt;br&gt;Weet u zeker dat u wilt afsluiten?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="78"/>
        <location filename="../main.qml" line="286"/>
        <source>Operating System</source>
        <translation>Besturingssysteem</translation>
    </message>
    <message>
        <location filename="../main.qml" line="90"/>
        <source>CHOOSE OS</source>
        <translation>SELECTEER OS</translation>
    </message>
    <message>
        <location filename="../main.qml" line="112"/>
        <location filename="../main.qml" line="594"/>
        <source>SD Card</source>
        <translation>SD kaart</translation>
    </message>
    <message>
        <location filename="../main.qml" line="124"/>
        <location filename="../main.qml" line="914"/>
        <source>CHOOSE SD CARD</source>
        <translation>SELECTEER SD KAART</translation>
    </message>
    <message>
        <location filename="../main.qml" line="151"/>
        <source>WRITE</source>
        <translation>SCHRIJF</translation>
    </message>
    <message>
        <location filename="../main.qml" line="166"/>
        <location filename="../main.qml" line="859"/>
        <source>Writing... %1%</source>
        <translation>Schrijven... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="204"/>
        <source>CANCEL WRITE</source>
        <translation>Annuleer schrijven</translation>
    </message>
    <message>
        <location filename="../main.qml" line="207"/>
        <location filename="../main.qml" line="856"/>
        <source>Cancelling...</source>
        <translation>Annuleren...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="218"/>
        <source>CANCEL VERIFY</source>
        <translation>ANNULEER VERIFICATIE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <location filename="../main.qml" line="879"/>
        <location filename="../main.qml" line="932"/>
        <source>Finalizing...</source>
        <translation>Afronden...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="348"/>
        <location filename="../main.qml" line="908"/>
        <source>Erase</source>
        <translation>Wissen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="349"/>
        <source>Format card as FAT32</source>
        <translation>Formatteer kaart als FAT32</translation>
    </message>
    <message>
        <location filename="../main.qml" line="356"/>
        <source>Use custom</source>
        <translation>Gebruik eigen bestand</translation>
    </message>
    <message>
        <location filename="../main.qml" line="357"/>
        <source>Select a custom .img from your computer</source>
        <translation>Selecteer een eigen .img bestand</translation>
    </message>
    <message>
        <location filename="../main.qml" line="364"/>
        <location filename="../main.qml" line="517"/>
        <source>Error parsing os_list.json</source>
        <translation>Fout bij parsen os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="388"/>
        <source>Back</source>
        <translation>Terug</translation>
    </message>
    <message>
        <location filename="../main.qml" line="389"/>
        <source>Go back to main menu</source>
        <translation>Terug naar hoofdmenu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="444"/>
        <source>Released: %1</source>
        <translation>Release datum: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="447"/>
        <source>Cached on your computer</source>
        <translation>Opgeslagen op computer</translation>
    </message>
    <message>
        <location filename="../main.qml" line="449"/>
        <source>Online - %1 GB download</source>
        <translation>Online %1 GB download</translation>
    </message>
    <message>
        <location filename="../main.qml" line="670"/>
        <source>Mounted as %1</source>
        <translation>Mounted op %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="785"/>
        <source>QUIT APP</source>
        <translation>AFSLUITEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="795"/>
        <source>CONTINUE</source>
        <translation>VERDER GAAN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="838"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Fout bij downloaden van lijst met besturingssystemen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="882"/>
        <source>Verifying... %1%</source>
        <translation>Verifiëren... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="900"/>
        <source>Error</source>
        <translation>Fout</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>Write Successful</source>
        <translation>Klaar met schrijven</translation>
    </message>
    <message>
        <location filename="../main.qml" line="909"/>
        <source>&lt;b&gt;%2&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%2&lt;/b&gt; is gewist&lt;br&gt;&lt;br&gt;U kunt nu de SD kaart uit de lezer halen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="911"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; is geschreven naar &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;U kunt nu de SD kaart uit de lezer halen</translation>
    </message>
</context>
</TS>
