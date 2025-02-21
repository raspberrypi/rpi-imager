<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>Fehler beim Entpacken des Archivs: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Das Betriebssystem band die FAT32-Partition nicht ein</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Fehler beim Wechseln in den Ordner &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Error writing to storage</source>
        <translation type="vanished">Fehler beim Schreiben auf den Speicher</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="119"/>
        <source>unmounting drive</source>
        <translation>Laufwerk wird ausgehängt</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="139"/>
        <source>opening drive</source>
        <translation>Laufwerk wird geöffnet</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="167"/>
        <source>Error running diskpart: %1</source>
        <translation>Fehler beim Ausführen von Diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="188"/>
        <source>Error removing existing partitions</source>
        <translation>Fehler beim Entfernen von existierenden Partitionen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="214"/>
        <source>Authentication cancelled</source>
        <translation>Authentifizierung abgebrochen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fehler beim Ausführen von authopen, um Zugriff auf Geräte zu erhalten &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="218"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>Not sure if current macOS has that option (or if it got moved/renamed)</translatorcomment>
        <translation type="unfinished">Bitte stellen Sie sicher, dass &apos;Raspberry Pi Imager&apos; Zugriff auf &apos;removable volumes&apos; in privacy settings hat (unter &apos;files and folders&apos;. Sie können ihm auch &apos;full disk access&apos; geben).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="240"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Speichergerät &apos;%1&apos; kann nicht geöffnet werden.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>discarding existing data on drive</source>
        <translation>Vorhandene Daten auf dem Medium werden gelöscht</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="302"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>Erstes und letztes Megabyte des Mediums werden überschrieben</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="308"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Schreibfehler während des Löschens des MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="320"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Fehler beim Löschen des letzten Teiles der Speicherkarte.&lt;br&gt;Die Speicherkarte könnte mit einer falschen Größe beworben sein (möglicherweise Betrug).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="409"/>
        <source>starting download</source>
        <translation>Download wird gestartet</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="469"/>
        <source>Error downloading: %1</source>
        <translation>Fehler beim Herunterladen: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="666"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Zugriff verweigert-Fehler beim Schreiben auf den Datenträger.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="671"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>&quot;Überwachter Ordnerzugriff&quot; scheint aktiviert zu sein. Bitte fügen Sie sowohl rpi-imager.exe als auch fat32format.exe zur Liste der erlaubten Apps hinzu und versuchen sie es erneut.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="678"/>
        <source>Error writing file to disk</source>
        <translation>Fehler beim Schreiben der Datei auf den Speicher</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="700"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Download beschädigt. Prüfsumme stimmt nicht überein</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="712"/>
        <location filename="../downloadthread.cpp" line="764"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während flushing)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <location filename="../downloadthread.cpp" line="771"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="754"/>
        <source>Error writing first block (partition table)</source>
        <translation>Fehler beim Schreiben des ersten Blocks (Partitionstabelle)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="829"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Fehler beim Lesen vom Speicher.&lt;br&gt;Die SD-Karte könnte defekt sein.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="848"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verifizierung fehlgeschlagen. Der Inhalt der SD-Karte weicht von dem Inhalt ab, der geschrieben werden sollte.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="901"/>
        <source>Customizing image</source>
        <translation>Image modifizieren</translation>
    </message>
    <message>
        <source>Waiting for FAT partition to be mounted</source>
        <translation type="vanished">Warten auf das Einbinden der FAT-Partition</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation type="vanished">Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation type="vanished">Das Betriebssystem hat die FAT32-Partition nicht eingebunden.</translation>
    </message>
    <message>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation type="vanished">Modifizieren fehlgeschlagen. Die Datei &apos;%1&apos; existiert nicht.</translation>
    </message>
    <message>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation type="vanished">Fehler beim Erstellen von firstrun.sh auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error writing to config.txt on FAT partition</source>
        <translation type="vanished">Fehler beim Schreiben in config.txt auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation type="vanished">Fehler beim Erstellen der user-data cloudinit Datei auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation type="vanished">Fehler beim Erstellen der network-config cloudinit Datei auf der FAT-Partition</translation>
    </message>
    <message>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation type="vanished">Fehler beim Schreiben in cmdline.txt auf der FAT-Partition</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Fehler beim Partitionieren: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Fehler beim Starten von fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Fehler beim Ausführen von fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Fehler beim Festlegen eines neuen Laufwerksbuchstabens</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Ungültiges Gerät: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Fehler beim Formatieren (mit udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Fehler beim Starten von sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>Partitionierung hat nicht die erwartete FAT-partition %1 erstellt</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Fehler beim Starten von mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Fehler beim Ausführen von mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatieren wird auf dieser Platform nicht unterstützt</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <location filename="../DstPopup.qml" line="24"/>
        <source>Storage</source>
        <translation type="unfinished">SD-Karte</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="38"/>
        <source>No storage devices found</source>
        <translation type="unfinished">Keine SD-Karte gefunden</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="67"/>
        <source>Exclude System Drives</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="94"/>
        <source>gigabytes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="96"/>
        <location filename="../DstPopup.qml" line="152"/>
        <source>Mounted as %1</source>
        <translation type="unfinished">Als %1 eingebunden</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="139"/>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="154"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[SCHREIBGESCHÜTZT]</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="156"/>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="197"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">Die Speicherkarte ist schreibgeschützt.&lt;br&gt;Schieben Sie den Schutzschalter auf der linken Seite nach oben, und versuchen Sie es erneut.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <location filename="../hwlistmodel.cpp" line="111"/>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">MODELL WÄHLEN</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <location filename="../HwPopup.qml" line="27"/>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Raspberry Pi Modell</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="301"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Die Speicherkapazität ist nicht groß genug.&lt;br&gt;Sie muss mindestens %1 GB betragen.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="307"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Die Eingabedatei ist kein gültiges Disk-Image.&lt;br&gt;Die Dateigröße%1 Bytes ist kein Vielfaches von 512 Bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="716"/>
        <source>Downloading and writing image</source>
        <translation>Image herunterladen und schreiben</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="814"/>
        <source>Select image</source>
        <translation>Image wählen</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="989"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Fehler beim Synchronisieren der Zeit. Neuer Versuch in 3 Sekunden</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1001"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>STP ist auf Ihrem Ethernet-Switch aktiviert. Das Abrufen der IP wird lange dauern.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1212"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Möchten Sie das Wifi-Passwort aus dem System-Schlüsselbund vorab ausfüllen?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="36"/>
        <source>opening image file</source>
        <translation>Abbilddatei wird geöffnet</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="41"/>
        <source>Error opening image file</source>
        <translation>Fehler beim Öffnen der Imagedatei</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="52"/>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="61"/>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="70"/>
        <source>CONTINUE</source>
        <translation>WEITER</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="78"/>
        <source>QUIT</source>
        <translation>BEENDEN</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <location filename="../oslistmodel.cpp" line="211"/>
        <source>Recommended</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <location filename="../OSPopup.qml" line="30"/>
        <source>Operating System</source>
        <translation type="unfinished">Betriebssystem (OS)</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="92"/>
        <source>Back</source>
        <translation type="unfinished">Zurück</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="93"/>
        <source>Go back to main menu</source>
        <translation type="unfinished">Zurück zum Hauptmenü</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="250"/>
        <source>Released: %1</source>
        <translation type="unfinished">Veröffentlicht: %1</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="260"/>
        <source>Cached on your computer</source>
        <translation type="unfinished">Auf Ihrem Computer zwischengespeichert</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="262"/>
        <source>Local file</source>
        <translation type="unfinished">Lokale Datei</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="263"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Online - %1 GB Download</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="349"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Verbinden Sie zuerst einen USB-Stick mit Images.&lt;br&gt;Die Images müssen sich im Wurzelverzeichnes des USB-Sticks befinden.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="40"/>
        <source>Set hostname:</source>
        <translation type="unfinished">Hostname:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="68"/>
        <source>Set username and password</source>
        <translation type="unfinished">Benutzername und Passwort festlegen</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="81"/>
        <source>Username:</source>
        <translation type="unfinished">Benutzername:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="106"/>
        <location filename="../OptionsGeneralTab.qml" line="183"/>
        <source>Password:</source>
        <translation type="unfinished">Passwort:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="147"/>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Wifi einrichten</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="161"/>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="212"/>
        <source>Show password</source>
        <translation type="unfinished">Passwort anzeigen</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="223"/>
        <source>Hidden SSID</source>
        <translation type="unfinished">Verborgene SSID</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="234"/>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Wifi-Land:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="252"/>
        <source>Set locale settings</source>
        <translation type="unfinished">Spracheinstellungen festlegen</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="256"/>
        <source>Time zone:</source>
        <translation type="unfinished">Zeitzone:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="275"/>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Tastaturlayout:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <location filename="../OptionsMiscTab.qml" line="23"/>
        <source>Play sound when finished</source>
        <translation type="unfinished">Tonsignal nach Beenden abspielen</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="27"/>
        <source>Eject media when finished</source>
        <translation type="unfinished">Medien nach Beenden auswerfen</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="31"/>
        <source>Enable telemetry</source>
        <translation type="unfinished">Telemetrie aktivieren</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="28"/>
        <source>OS Customization</source>
        <translation>OS Anpassungen</translation>
    </message>
    <message>
        <source>OS customization options</source>
        <translation type="vanished">OS Anpassungen Optionen</translation>
    </message>
    <message>
        <source>for this session only</source>
        <translation type="vanished">nur für diese Sitzung</translation>
    </message>
    <message>
        <source>to always use</source>
        <translation type="vanished">immer verwenden</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="65"/>
        <source>General</source>
        <translation>Allgemein</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="74"/>
        <source>Services</source>
        <translation>Dienste</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="80"/>
        <source>Options</source>
        <translation>Optionen</translation>
    </message>
    <message>
        <source>Set hostname:</source>
        <translation type="vanished">Hostname:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="vanished">Benutzername und Passwort festlegen</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="vanished">Benutzername:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="vanished">Passwort:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="vanished">Wifi einrichten</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="vanished">SSID:</translation>
    </message>
    <message>
        <source>Show password</source>
        <translation type="vanished">Passwort anzeigen</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="vanished">Verborgene SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="vanished">Wifi-Land:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="vanished">Spracheinstellungen festlegen</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="vanished">Zeitzone:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="vanished">Tastaturlayout:</translation>
    </message>
    <message>
        <source>Enable SSH</source>
        <translation type="vanished">SSH aktivieren</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="vanished">Passwort zur Authentifizierung verwenden</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="vanished">Authentifizierung via Public-Key</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="vanished">authorized_keys für &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="vanished">SSH-KEYGEN ausführen</translation>
    </message>
    <message>
        <source>Play sound when finished</source>
        <translation type="vanished">Tonsignal nach Beenden abspielen</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="vanished">Medien nach Beenden auswerfen</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="vanished">Telemetrie aktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="123"/>
        <source>SAVE</source>
        <translation>SPEICHERN</translation>
    </message>
    <message>
        <source>Disable overscan</source>
        <translation type="vanished">Overscan deaktivieren</translation>
    </message>
    <message>
        <source>Set password for &apos;%1&apos; user:</source>
        <translation type="vanished">Passwort für &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Skip first-run wizard</source>
        <translation type="vanished">Einrichtungsassistent überspringen</translation>
    </message>
    <message>
        <source>Persistent settings</source>
        <translation type="vanished">Dauerhafte Einstellungen</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <location filename="../OptionsServicesTab.qml" line="36"/>
        <source>Enable SSH</source>
        <translation type="unfinished">SSH aktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="52"/>
        <source>Use password authentication</source>
        <translation type="unfinished">Passwort zur Authentifizierung verwenden</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="63"/>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Authentifizierung via Public-Key</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="74"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">authorized_keys für &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="121"/>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="140"/>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">SSH-KEYGEN ausführen</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="150"/>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="126"/>
        <source>Internal SD card reader</source>
        <translation>Interner SD-Kartenleser</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Use OS customization?</source>
        <translation type="vanished">OS Anpassungen anwenden?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="41"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Möchten Sie die vorher festgelegten OS Anpassungen anwenden?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="82"/>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="63"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NEIN, EINSTELLUNGEN LÖSCHEN</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="73"/>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="50"/>
        <source>EDIT SETTINGS</source>
        <translation>EINSTELLUNGEN BEARBEITEN</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="27"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="120"/>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Pi Modell</translation>
    </message>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="vanished">MODELL WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="144"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Klicken Sie auf diesen Knopf, um den gewünschten Raspberry Pi auszuwählen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="158"/>
        <source>Operating System</source>
        <translation>Betriebssystem (OS)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="169"/>
        <location filename="../main.qml" line="446"/>
        <source>CHOOSE OS</source>
        <translation>OS WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="182"/>
        <source>Select this button to change the operating system</source>
        <translation>Klicken Sie auf diesen Knopf, um das Betriebssystem zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="196"/>
        <source>Storage</source>
        <translation>SD-Karte</translation>
    </message>
    <message>
        <location filename="../main.qml" line="331"/>
        <source>Network not ready yet</source>
        <translation>Netzwerk noch nicht bereit</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="vanished">Keine SD-Karte gefunden</translation>
    </message>
    <message>
        <location filename="../main.qml" line="207"/>
        <location filename="../main.qml" line="668"/>
        <source>CHOOSE STORAGE</source>
        <translation>SD-KARTE WÄHLEN</translation>
    </message>
    <message>
        <source>WRITE</source>
        <translation type="vanished">SCHREIBEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Klicken Sie auf diesen Knopf, um das Ziel-Speichermedium zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="266"/>
        <source>CANCEL WRITE</source>
        <translation>SCHREIBEN ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="269"/>
        <location filename="../main.qml" line="591"/>
        <source>Cancelling...</source>
        <translation>Abbrechen...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="281"/>
        <source>CANCEL VERIFY</source>
        <translation>VERIFIZIERUNG ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="284"/>
        <location filename="../main.qml" line="614"/>
        <location filename="../main.qml" line="687"/>
        <source>Finalizing...</source>
        <translation>Finalisieren...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="293"/>
        <source>Next</source>
        <translation>Weiter</translation>
    </message>
    <message>
        <location filename="../main.qml" line="299"/>
        <source>Select this button to start writing the image</source>
        <translation>Klicken Sie auf diesen Knopf, um mit dem Schreiben zu beginnen</translation>
    </message>
    <message>
        <source>Select this button to access advanced settings</source>
        <translation type="vanished">Klicken Sie auf diesen Knopf, um zu den erweiterten Einstellungen zu gelangen.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="321"/>
        <source>Using custom repository: %1</source>
        <translation>Verwende benutzerdefiniertes Repository: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="340"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Tastaturnavigation: &lt;Tab&gt; zum nächsten Knopf navigieren &lt;Leertaste&gt; Knopf drücken/Element auswählen &lt;Pfeil hoch/runter&gt; in Listen nach oben/unten gehen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="361"/>
        <source>Language: </source>
        <translation>Sprache: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="384"/>
        <source>Keyboard: </source>
        <translation>Tastatur: </translation>
    </message>
    <message>
        <source>Pi model:</source>
        <translation type="vanished">Pi Modell:</translation>
    </message>
    <message>
        <source>[ All ]</source>
        <translation type="vanished">[ Alle ]</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="vanished">Zurück</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="vanished">Zurück zum Hauptmenü</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="vanished">Veröffentlicht: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="vanished">Auf Ihrem Computer zwischengespeichert</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="vanished">Lokale Datei</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="vanished">Online - %1 GB Download</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="vanished">Als %1 eingebunden</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="vanished">[SCHREIBGESCHÜTZT]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="485"/>
        <source>Are you sure you want to quit?</source>
        <translation>Sind Sie sicher, dass Sie beenden möchten?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="486"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Der Raspberry Pi Imager ist noch beschäftigt. &lt;br&gt;Möchten Sie wirklich beenden?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="497"/>
        <source>Warning</source>
        <translation>Warnung</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Preparing to write...</source>
        <translation>Schreiben wird vorbereitet...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="520"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Alle vorhandenen Daten auf &apos;%1&apos; werden gelöscht.&lt;br&gt;Möchten Sie wirklich fortfahren?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="535"/>
        <source>Update available</source>
        <translation>Update verfügbar</translation>
    </message>
    <message>
        <location filename="../main.qml" line="536"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Eine neuere Version von Imager ist verfügbar.&lt;br&gt;Möchten Sie die Webseite besuchen, um das Update herunterzuladen?</translation>
    </message>
    <message>
        <source>Error downloading OS list from Internet</source>
        <translation type="vanished">Fehler beim Herunterladen der Betriebssystemsliste aus dem Internet</translation>
    </message>
    <message>
        <location filename="../main.qml" line="594"/>
        <source>Writing... %1%</source>
        <translation>Schreiben... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="617"/>
        <source>Verifying... %1%</source>
        <translation>Verifizieren... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="624"/>
        <source>Preparing to write... (%1)</source>
        <translation>Schreiben wird vorbereitet... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="644"/>
        <source>Error</source>
        <translation>Fehler</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Write Successful</source>
        <translation>Schreiben erfolgreich</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <location filename="../imagewriter.cpp" line="648"/>
        <source>Erase</source>
        <translation>Löschen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="653"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde geleert&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="660"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <source>Error parsing os_list.json</source>
        <translation type="vanished">Fehler beim Parsen von os_list.json</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="649"/>
        <source>Format card as FAT32</source>
        <translation>Karte als FAT32 formatieren</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="655"/>
        <source>Use custom</source>
        <translation>Eigenes Image</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="656"/>
        <source>Select a custom .img from your computer</source>
        <translation>Wählen Sie eine eigene .img-Datei von Ihrem Computer</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="vanished">Verbinden Sie zuerst einen USB-Stick mit Images.&lt;br&gt;Die Images müssen sich im Wurzelverzeichnes des USB-Sticks befinden.</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="vanished">Die Speicherkarte ist schreibgeschützt.&lt;br&gt;Schieben Sie den Schutzschalter auf der linken Seite nach oben, und versuchen Sie es erneut.</translation>
    </message>
    <message>
        <source>Select this button to change the destination SD card</source>
        <translation type="vanished">Klicke auf diesen Knopf, um die Ziel-SD-Karte zu ändern</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;</source>
        <translation type="vanished">&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben</translation>
    </message>
</context>
</TS>
