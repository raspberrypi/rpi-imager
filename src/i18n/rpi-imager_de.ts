<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="171"/>
        <source>Error writing to storage</source>
        <translation>Fehler beim Schreiben auf den Speicher</translation>
    </message>
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
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="114"/>
        <source>opening drive</source>
        <translation>Gerät wird geöffnet</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="147"/>
        <source>Error running diskpart: %1</source>
        <translation>Fehler beim Ausführen von Diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="168"/>
        <source>Error removing existing partitions</source>
        <translation>Fehler beim Entfernen von existierenden Partitionen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="194"/>
        <source>Authentication cancelled</source>
        <translation>Authentifizierung abgebrochen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="197"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fehler beim Ausführen von authopen, um Zugriff auf Geräte zu erhalten &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="198"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>I don&apos;t use Mac OS, I would need help here. Unfinished translation:

Bitte stellen Sie sicher, dass &apos;Raspberry Pi Imager&apos; Zugriff auf &apos;removable volumes&apos; in privacy settings hat (unter &apos;files and folders&apos;. Sie können ihm auch &apos;full disk access&apos; geben).</translatorcomment>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="220"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Speichergerät &apos;%1&apos; kann nicht geöffnet werden.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="262"/>
        <source>discarding existing data on drive</source>
        <translation>Vorhandene Daten auf dem Medium werden gelöscht</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>Erstes und letztes Megabyte des Mediums werden überschrieben</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="288"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Schreibfehler während des Löschens des MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="300"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Fehler beim Löschen des letzten Teiles der Speicherkarte.&lt;br&gt;Die Speicherkarte könnte mit einer falschen Größe beworben sein (möglicherweise Betrug).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="389"/>
        <source>starting download</source>
        <translation>Download wird gestartet</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="779"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Fehler beim Lesen vom Speicher.&lt;br&gt;Die SD-Karte könnte defekt sein.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="855"/>
        <source>Waiting for FAT partition to be mounted</source>
        <translation>Warten auf das Einbinden der FAT-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="941"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="963"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Das Betriebssystem hat die FAT32-Partition nicht eingebunden.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="996"/>
        <source>Unable to customize. File &apos;%1&apos; does not exist.</source>
        <translation>Modifizieren fehlgeschlagen. Die Datei &apos;%1&apos; existiert nicht.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1000"/>
        <source>Customizing image</source>
        <translation>Image modifizieren</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1085"/>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation>Fehler beim Erstellen von firstrun.sh auf der FAT-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1037"/>
        <source>Error writing to config.txt on FAT partition</source>
        <translation>Fehler beim Schreiben in config.txt auf der FAT-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1102"/>
        <source>Error creating user-data cloudinit file on FAT partition</source>
        <translation>Fehler beim Erstellen der user-data cloudinit Datei auf der FAT-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1116"/>
        <source>Error creating network-config cloudinit file on FAT partition</source>
        <translation>Fehler beim Erstellen der network-config cloudinit Datei auf der FAT-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="1139"/>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation>Fehler beim Schreiben in cmdline.txt auf der FAT-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="432"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Zugriff verweigert-Fehler beim Schreiben auf den Datenträger.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="437"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translatorcomment>I don&apos;t use Windows either. What is &quot;Controlled Folder Access&quot; in the German version?

Controlled Folder Access scheint aktiviert zu sein. Bitte fügen Sie sowohl rpi-imager.exe als auch fat32format.exe zur Liste der erlaubten Apps hinzu und versuchen sie es erneut.</translatorcomment>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="444"/>
        <source>Error writing file to disk</source>
        <translation>Fehler beim Schreiben der Datei auf den Speicher</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="463"/>
        <source>Error downloading: %1</source>
        <translation>Fehler beim Herunterladen: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="674"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Download beschädigt. Prüfsumme stimmt nicht überein</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="686"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während flushing)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="693"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <source>Error writing first block (partition table)</source>
        <translation>Fehler beim Schreiben auf des ersten Blocks (Partitionstabelle)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="798"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verifizierung fehlgeschlagen. Der Inhalt der SD-Karte weicht von dem Inhalt ab, der geschrieben werden sollte.</translation>
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
        <translation>Fehler beim Verwenden von fat32format: %1</translation>
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
        <translation>Fehler beim Verwenden von mkfs.fat: %1</translation>
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
        <location filename="../imagewriter.cpp" line="257"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Die Speicherkapazität ist nicht groß genug.&lt;br&gt;Sie muss mindestens %1 GB betragen.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="263"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Die Eingabedatei ist kein gültiges Disk-Image.&lt;br&gt;Die Dateigröße%1 Bytes ist kein Vielfaches von 512 Bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="445"/>
        <source>Downloading and writing image</source>
        <translation>Image herunterladen und schreiben</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="578"/>
        <source>Select image</source>
        <translation>Image wählen</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="979"/>
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
        <location filename="../OptionsPopup.qml" line="79"/>
        <source>Advanced options</source>
        <translation>Erweiterte Optionen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="96"/>
        <source>Image customization options</source>
        <translation>OS-Modifizierungen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="104"/>
        <source>for this session only</source>
        <translation>Nur für diese Sitzung</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="105"/>
        <source>to always use</source>
        <translation>Immer verwenden</translation>
    </message>
    <message>
        <source>Disable overscan</source>
        <translation type="vanished">Overscan deaktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="119"/>
        <source>Set hostname:</source>
        <translation>Hostname:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="138"/>
        <source>Enable SSH</source>
        <translation>SSH aktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="160"/>
        <source>Use password authentication</source>
        <translation>Password zur Authentifizierung verwenden</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="170"/>
        <source>Allow public-key authentication only</source>
        <translation>Authentifizierung via Public-Key</translation>
    </message>
    <message>
        <source>Set password for &apos;%1&apos; user:</source>
        <translation type="vanished">Passwort für &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="188"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>authorized_keys für &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="261"/>
        <source>Configure wireless LAN</source>
        <translation>Wifi einrichten</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="280"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="234"/>
        <location filename="../OptionsPopup.qml" line="300"/>
        <source>Password:</source>
        <translation>Passwort:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="200"/>
        <source>Set username and password</source>
        <translation>Benutzername und Passwort setzen:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="219"/>
        <source>Username:</source>
        <translation>Benutzername:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="295"/>
        <source>Hidden SSID</source>
        <translation>Verborgene SSID</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="316"/>
        <source>Show password</source>
        <translation>Passwort anzeigen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="321"/>
        <source>Wireless LAN country:</source>
        <translation>Wifi-Land:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="332"/>
        <source>Set locale settings</source>
        <translation>Spracheinstellungen festlegen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="342"/>
        <source>Time zone:</source>
        <translation>Zeitzone:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="352"/>
        <source>Keyboard layout:</source>
        <translation>Tastaturlayout:</translation>
    </message>
    <message>
        <source>Skip first-run wizard</source>
        <translation type="vanished">Einrichtungsassistent überspringen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="365"/>
        <source>Persistent settings</source>
        <translation>Dauerhafte Einstellungen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="373"/>
        <source>Play sound when finished</source>
        <translation>Tonsignal nach Beenden abspielen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="377"/>
        <source>Eject media when finished</source>
        <translation>Medien nach Beenden auswerfen</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="381"/>
        <source>Enable telemetry</source>
        <translation>Telemetry aktivieren</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="394"/>
        <source>SAVE</source>
        <translation>SPEICHERN</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="111"/>
        <source>Internal SD card reader</source>
        <translation>Interner SD-Kartenleser</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="72"/>
        <source>Warning: advanced settings set</source>
        <translation>Warnung: Erweiterte Optionen festgelegt</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="86"/>
        <source>Would you like to apply the image customization settings saved earlier?</source>
        <translation>Möchten Sie die vorher festgelegten OS-Modifizierungen anwenden?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="95"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NEIN, EINSTELLUNGEN LÖSCHEN</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="105"/>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>EDIT SETTINGS</source>
        <translation>EINSTELLUNGEN BEARBEITEN</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <location filename="../main.qml" line="24"/>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="99"/>
        <location filename="../main.qml" line="399"/>
        <source>Operating System</source>
        <translation>Betriebssystem</translation>
    </message>
    <message>
        <location filename="../main.qml" line="111"/>
        <source>CHOOSE OS</source>
        <translation>OS WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="123"/>
        <source>Select this button to change the operating system</source>
        <translation>Klicke auf diesen Knopf, um das Betriebssystem zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="135"/>
        <location filename="../main.qml" line="713"/>
        <source>Storage</source>
        <translation>SD-Karte</translation>
    </message>
    <message>
        <location filename="../main.qml" line="147"/>
        <location filename="../main.qml" line="1038"/>
        <source>CHOOSE STORAGE</source>
        <translation>SD-KARTE WÄHLEN</translation>
    </message>
    <message>
        <source>Select this button to change the destination SD card</source>
        <translation type="vanished">Klicke auf diesen Knopf, um die Ziel-SD-Karte zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="173"/>
        <source>WRITE</source>
        <translation>SCHREIBEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="177"/>
        <source>Select this button to start writing the image</source>
        <translation>Klicke auf diesen Knopf, um mit dem Schreiben zu beginnen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="218"/>
        <source>CANCEL WRITE</source>
        <translation>SCHREIBEN ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <location filename="../main.qml" line="965"/>
        <source>Cancelling...</source>
        <translation>Abbrechen...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="229"/>
        <source>CANCEL VERIFY</source>
        <translation>VERIFIZIERUNG ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="232"/>
        <location filename="../main.qml" line="988"/>
        <location filename="../main.qml" line="1057"/>
        <source>Finalizing...</source>
        <translation>Finalisieren...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="505"/>
        <location filename="../main.qml" line="1022"/>
        <source>Erase</source>
        <translation>Löschen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Format card as FAT32</source>
        <translation>Karte als FAT32 formatieren</translation>
    </message>
    <message>
        <location filename="../main.qml" line="515"/>
        <source>Use custom</source>
        <translation>Eigenes Image</translation>
    </message>
    <message>
        <location filename="../main.qml" line="516"/>
        <source>Select a custom .img from your computer</source>
        <translation>Wählen Sie eine eigene .img-Datei von Ihrem Computer</translation>
    </message>
    <message>
        <location filename="../main.qml" line="461"/>
        <source>Back</source>
        <translation>Zurück</translation>
    </message>
    <message>
        <location filename="../main.qml" line="157"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Klicken Sie auf diesen Knopf, um das Ziel-Speichermedium zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="247"/>
        <source>Select this button to access advanced settings</source>
        <translation>Klicken Sie auf diesen Knopf, um zu den erweiterten Einstellungen zu gelangen.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="261"/>
        <source>Using custom repository: %1</source>
        <translation>Verwende benutzerdefiniertes Repository: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="270"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="290"/>
        <source>Language: </source>
        <translation>Sprache: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="313"/>
        <source>Keyboard: </source>
        <translation>Tastatur: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="462"/>
        <source>Go back to main menu</source>
        <translation>Zurück zum Hauptmenü</translation>
    </message>
    <message>
        <location filename="../main.qml" line="628"/>
        <source>Released: %1</source>
        <translation>Veröffentlicht: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="638"/>
        <source>Cached on your computer</source>
        <translation>Auf Ihrem Computer zwischengespeichert</translation>
    </message>
    <message>
        <location filename="../main.qml" line="640"/>
        <source>Local file</source>
        <translation>Lokale Datei</translation>
    </message>
    <message>
        <location filename="../main.qml" line="641"/>
        <source>Online - %1 GB download</source>
        <translation>Online - %1 GB Download</translation>
    </message>
    <message>
        <location filename="../main.qml" line="766"/>
        <location filename="../main.qml" line="818"/>
        <location filename="../main.qml" line="824"/>
        <source>Mounted as %1</source>
        <translation>Als %1 eingebunden</translation>
    </message>
    <message>
        <location filename="../main.qml" line="820"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[SCHREIBGESCHÜTZT]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="862"/>
        <source>Are you sure you want to quit?</source>
        <translation>Sind Sie sicher, dass Sie beenden möchten?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="863"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Der Raspberry Pi Imager ist noch beschäftigt. &lt;br&gt;Möchten Sie wirklich beenden?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="874"/>
        <source>Warning</source>
        <translation>Warnung</translation>
    </message>
    <message>
        <location filename="../main.qml" line="882"/>
        <source>Preparing to write...</source>
        <translation>Schreiben wird vorbereitet...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Update available</source>
        <translation>Update verfügbar</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Eine neuere Version von Imager ist verfügbar. &lt;br&gt;Möchten Sie die Webseite besuchen, um das Update herunterzuladen?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="968"/>
        <source>Writing... %1%</source>
        <translation>Schreiben... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="895"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Alle vorhandenen Daten auf &apos;%1&apos; werden gelöscht.&lt;br&gt;Möchten Sie wirklich fortfahren?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="947"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Fehler beim Herunterladen der Betriebssystemsliste aus dem Internet</translation>
    </message>
    <message>
        <location filename="../main.qml" line="991"/>
        <source>Verifying... %1%</source>
        <translation>Verifizierung... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="998"/>
        <source>Preparing to write... (%1)</source>
        <translation>Schreiben wird vorbereitet... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1014"/>
        <source>Error</source>
        <translation>Fehler</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1021"/>
        <source>Write Successful</source>
        <translation>Schreiben erfolgreich</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1023"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde geleert&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;</source>
        <translation type="vanished">&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1030"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1098"/>
        <source>Error parsing os_list.json</source>
        <translation>Fehler beim Parsen von os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1271"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Verbinden Sie zuerst einen USB-Stick mit Images.&lt;br&gt;Die Images müssen sich im Wurzelverzeichnes des USB-Sticks befinden.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1287"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>Die Speicherkarte ist schreibgeschützt.&lt;br&gt;Schieben Sie den Schutzschalter auf der linken Seite nach oben, und versuchen Sie es erneut.</translation>
    </message>
</context>
</TS>
