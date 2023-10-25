<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="196"/>
        <location filename="../downloadextractthread.cpp" line="385"/>
        <source>Error extracting archive: %1</source>
        <translation>Fehler beim Entpacken des Archivs: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="261"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="281"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Das Betriebssystem band die FAT32-Partition nicht ein</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="304"/>
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
        <location filename="../downloadthread.cpp" line="118"/>
        <source>unmounting drive</source>
        <translation>Laufwerk wird ausgehängt</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="138"/>
        <source>opening drive</source>
        <translation>Laufwerk wird geöffnet</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="166"/>
        <source>Error running diskpart: %1</source>
        <translation>Fehler beim Ausführen von Diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="187"/>
        <source>Error removing existing partitions</source>
        <translation>Fehler beim Entfernen von existierenden Partitionen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="213"/>
        <source>Authentication cancelled</source>
        <translation>Authentifizierung abgebrochen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="216"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fehler beim Ausführen von authopen, um Zugriff auf Geräte zu erhalten &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>Not sure if current macOS has that option (or if it got moved/renamed)</translatorcomment>
        <translation type="unfinished">Bitte stellen Sie sicher, dass &apos;Raspberry Pi Imager&apos; Zugriff auf &apos;removable volumes&apos; in privacy settings hat (unter &apos;files and folders&apos;. Sie können ihm auch &apos;full disk access&apos; geben).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="239"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Speichergerät &apos;%1&apos; kann nicht geöffnet werden.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="281"/>
        <source>discarding existing data on drive</source>
        <translation>Vorhandene Daten auf dem Medium werden gelöscht</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="301"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>Erstes und letztes Megabyte des Mediums werden überschrieben</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="307"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Schreibfehler während des Löschens des MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="319"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Fehler beim Löschen des letzten Teiles der Speicherkarte.&lt;br&gt;Die Speicherkarte könnte mit einer falschen Größe beworben sein (möglicherweise Betrug).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="408"/>
        <source>starting download</source>
        <translation>Download wird gestartet</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="466"/>
        <source>Error downloading: %1</source>
        <translation>Fehler beim Herunterladen: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="663"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Zugriff verweigert-Fehler beim Schreiben auf den Datenträger.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="668"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>&quot;Überwachter Ordnerzugriff&quot; scheint aktiviert zu sein. Bitte fügen Sie sowohl rpi-imager.exe als auch fat32format.exe zur Liste der erlaubten Apps hinzu und versuchen sie es erneut.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="675"/>
        <source>Error writing file to disk</source>
        <translation>Fehler beim Schreiben der Datei auf den Speicher</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="697"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Download beschädigt. Prüfsumme stimmt nicht überein</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="709"/>
        <location filename="../downloadthread.cpp" line="761"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während flushing)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="716"/>
        <location filename="../downloadthread.cpp" line="768"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="751"/>
        <source>Error writing first block (partition table)</source>
        <translation>Fehler beim Schreiben des ersten Blocks (Partitionstabelle)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="826"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Fehler beim Lesen vom Speicher.&lt;br&gt;Die SD-Karte könnte defekt sein.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="845"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verifizierung fehlgeschlagen. Der Inhalt der SD-Karte weicht von dem Inhalt ab, der geschrieben werden sollte.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="898"/>
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
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="248"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Die Speicherkapazität ist nicht groß genug.&lt;br&gt;Sie muss mindestens %1 GB betragen.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="254"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Die Eingabedatei ist kein gültiges Disk-Image.&lt;br&gt;Die Dateigröße%1 Bytes ist kein Vielfaches von 512 Bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="442"/>
        <source>Downloading and writing image</source>
        <translation>Image herunterladen und schreiben</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="575"/>
        <source>Select image</source>
        <translation>Image wählen</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="896"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Möchten Sie das Wifi-Passwort aus dem System-Schlüsselbund vorab ausfüllen?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>Abbilddatei wird geöffnet</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>Fehler beim Öffnen der Imagedatei</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="120"/>
        <source>CONTINUE</source>
        <translation>WEITER</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="130"/>
        <source>QUIT</source>
        <translation>BEENDEN</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="20"/>
        <source>OS Customization</source>
        <translation>OS Anpassungen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="52"/>
        <source>OS customization options</source>
        <translation>OS Anpassungen Optionen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="57"/>
        <source>for this session only</source>
        <translation>nur für diese Sitzung</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="58"/>
        <source>to always use</source>
        <translation>immer verwenden</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="71"/>
        <source>General</source>
        <translation>Allgemein</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="79"/>
        <source>Services</source>
        <translation>Dienste</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="82"/>
        <source>Options</source>
        <translation>Optionen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="98"/>
        <source>Set hostname:</source>
        <translation>Hostname:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="120"/>
        <source>Set username and password</source>
        <translation>Benutzername und Passwort festlegen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="142"/>
        <source>Username:</source>
        <translation>Benutzername:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="158"/>
        <location filename="../OptionsPopup.qml" line="219"/>
        <source>Password:</source>
        <translation>Passwort:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="186"/>
        <source>Configure wireless LAN</source>
        <translation>Wifi einrichten</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="205"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="238"/>
        <source>Show password</source>
        <translation>Passwort anzeigen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="244"/>
        <source>Hidden SSID</source>
        <translation>Verborgene SSID</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="250"/>
        <source>Wireless LAN country:</source>
        <translation>Wifi-Land:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="261"/>
        <source>Set locale settings</source>
        <translation>Spracheinstellungen festlegen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="271"/>
        <source>Time zone:</source>
        <translation>Zeitzone:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="281"/>
        <source>Keyboard layout:</source>
        <translation>Tastaturlayout:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="298"/>
        <source>Enable SSH</source>
        <translation>SSH aktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="317"/>
        <source>Use password authentication</source>
        <translation>Passwort zur Authentifizierung verwenden</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="327"/>
        <source>Allow public-key authentication only</source>
        <translation>Authentifizierung via Public-Key</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="345"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>authorized_keys für &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="357"/>
        <source>RUN SSH-KEYGEN</source>
        <translation>SSH-KEYGEN ausführen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="375"/>
        <source>Play sound when finished</source>
        <translation>Tonsignal nach Beenden abspielen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="379"/>
        <source>Eject media when finished</source>
        <translation>Medien nach Beenden auswerfen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="383"/>
        <source>Enable telemetry</source>
        <translation>Telemetrie aktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="397"/>
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
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="119"/>
        <source>Internal SD card reader</source>
        <translation>Interner SD-Kartenleser</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="73"/>
        <source>Use OS customization?</source>
        <translation>OS Anpassungen anwenden?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="87"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Möchten Sie die vorher festgelegten OS Anpassungen anwenden?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="97"/>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="107"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NEIN, EINSTELLUNGEN LÖSCHEN</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="117"/>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="127"/>
        <source>EDIT SETTINGS</source>
        <translation>EINSTELLUNGEN BEARBEITEN</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="22"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="114"/>
        <location filename="../main.qml" line="467"/>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Pi Modell</translation>
    </message>
    <message>
        <location filename="../main.qml" line="126"/>
        <source>CHOOSE DEVICE</source>
        <translation>MODELL WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="138"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Klicken Sie auf diesen Knopf, um den gewünschten Raspberry Pi auszuwählen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="97"/>
        <location filename="../main.qml" line="413"/>
        <source>Operating System</source>
        <translation>Betriebssystem (OS)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="109"/>
        <source>CHOOSE OS</source>
        <translation>OS WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="121"/>
        <source>Select this button to change the operating system</source>
        <translation>Klicken Sie auf diesen Knopf, um das Betriebssystem zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="133"/>
        <location filename="../main.qml" line="780"/>
        <source>Storage</source>
        <translation>SD-Karte</translation>
    </message>
    <message>
        <location filename="../main.qml" line="145"/>
        <location filename="../main.qml" line="1108"/>
        <source>CHOOSE STORAGE</source>
        <translation>SD-KARTE WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="171"/>
        <source>WRITE</source>
        <translation>SCHREIBEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="155"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Klicken Sie auf diesen Knopf, um das Ziel-Speichermedium zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="216"/>
        <source>CANCEL WRITE</source>
        <translation>SCHREIBEN ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="219"/>
        <location filename="../main.qml" line="1035"/>
        <source>Cancelling...</source>
        <translation>Abbrechen...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="227"/>
        <source>CANCEL VERIFY</source>
        <translation>VERIFIZIERUNG ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="230"/>
        <location filename="../main.qml" line="1058"/>
        <location filename="../main.qml" line="1127"/>
        <source>Finalizing...</source>
        <translation>Finalisieren...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="288"/>
        <source>Next</source>
        <translation>Weiter</translation>
    </message>
    <message>
        <location filename="../main.qml" line="175"/>
        <source>Select this button to start writing the image</source>
        <translation>Klicken Sie auf diesen Knopf, um mit dem Schreiben zu beginnen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="245"/>
        <source>Select this button to access advanced settings</source>
        <translation>Klicken Sie auf diesen Knopf, um zu den erweiterten Einstellungen zu gelangen.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="259"/>
        <source>Using custom repository: %1</source>
        <translation>Verwende benutzerdefiniertes Repository: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="268"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="289"/>
        <source>Language: </source>
        <translation>Sprache: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="312"/>
        <source>Keyboard: </source>
        <translation>Tastatur: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="437"/>
        <source>Pi model:</source>
        <translation>Pi Modell:</translation>
    </message>
    <message>
        <location filename="../main.qml" line="448"/>
        <source>[ All ]</source>
        <translation>[ Alle ]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="528"/>
        <source>Back</source>
        <translation>Zurück</translation>
    </message>
    <message>
        <location filename="../main.qml" line="529"/>
        <source>Go back to main menu</source>
        <translation>Zurück zum Hauptmenü</translation>
    </message>
    <message>
        <location filename="../main.qml" line="695"/>
        <source>Released: %1</source>
        <translation>Veröffentlicht: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="705"/>
        <source>Cached on your computer</source>
        <translation>Auf Ihrem Computer zwischengespeichert</translation>
    </message>
    <message>
        <location filename="../main.qml" line="707"/>
        <source>Local file</source>
        <translation>Lokale Datei</translation>
    </message>
    <message>
        <location filename="../main.qml" line="708"/>
        <source>Online - %1 GB download</source>
        <translation>Online - %1 GB Download</translation>
    </message>
    <message>
        <location filename="../main.qml" line="833"/>
        <location filename="../main.qml" line="885"/>
        <location filename="../main.qml" line="891"/>
        <source>Mounted as %1</source>
        <translation>Als %1 eingebunden</translation>
    </message>
    <message>
        <location filename="../main.qml" line="887"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[SCHREIBGESCHÜTZT]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="929"/>
        <source>Are you sure you want to quit?</source>
        <translation>Sind Sie sicher, dass Sie beenden möchten?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="930"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Der Raspberry Pi Imager ist noch beschäftigt. &lt;br&gt;Möchten Sie wirklich beenden?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="941"/>
        <source>Warning</source>
        <translation>Warnung</translation>
    </message>
    <message>
        <location filename="../main.qml" line="949"/>
        <source>Preparing to write...</source>
        <translation>Schreiben wird vorbereitet...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="962"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Alle vorhandenen Daten auf &apos;%1&apos; werden gelöscht.&lt;br&gt;Möchten Sie wirklich fortfahren?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="973"/>
        <source>Update available</source>
        <translation>Update verfügbar</translation>
    </message>
    <message>
        <location filename="../main.qml" line="974"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Eine neuere Version von Imager ist verfügbar.&lt;br&gt;Möchten Sie die Webseite besuchen, um das Update herunterzuladen?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1017"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Fehler beim Herunterladen der Betriebssystemsliste aus dem Internet</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1038"/>
        <source>Writing... %1%</source>
        <translation>Schreiben... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1061"/>
        <source>Verifying... %1%</source>
        <translation>Verifizieren... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1068"/>
        <source>Preparing to write... (%1)</source>
        <translation>Schreiben wird vorbereitet... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1084"/>
        <source>Error</source>
        <translation>Fehler</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1091"/>
        <source>Write Successful</source>
        <translation>Schreiben erfolgreich</translation>
    </message>
    <message>
        <location filename="../main.qml" line="572"/>
        <location filename="../main.qml" line="1092"/>
        <source>Erase</source>
        <translation>Löschen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1093"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde geleert&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1100"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1202"/>
        <source>Error parsing os_list.json</source>
        <translation>Fehler beim Parsen von os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="573"/>
        <source>Format card as FAT32</source>
        <translation>Karte als FAT32 formatieren</translation>
    </message>
    <message>
        <location filename="../main.qml" line="582"/>
        <source>Use custom</source>
        <translation>Eigenes Image</translation>
    </message>
    <message>
        <location filename="../main.qml" line="583"/>
        <source>Select a custom .img from your computer</source>
        <translation>Wählen Sie eine eigene .img-Datei von Ihrem Computer</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1391"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Verbinden Sie zuerst einen USB-Stick mit Images.&lt;br&gt;Die Images müssen sich im Wurzelverzeichnes des USB-Sticks befinden.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1407"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>Die Speicherkarte ist schreibgeschützt.&lt;br&gt;Schieben Sie den Schutzschalter auf der linken Seite nach oben, und versuchen Sie es erneut.</translation>
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
