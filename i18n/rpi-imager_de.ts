<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="167"/>
        <source>Error writing to storage</source>
        <translation>Fehler beim Schreiben auf den Speicher</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="189"/>
        <location filename="../downloadextractthread.cpp" line="349"/>
        <source>Error extracting archive: %1</source>
        <translation>Fehler beim Entpacken des Archivs: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="235"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="245"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Das Betriebssystem band die FAT32-Partition nicht ein</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="268"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Fehler beim Wechseln in den Ordner &quot;%1&quot;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="113"/>
        <source>opening drive</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="146"/>
        <source>Error running diskpart: %1</source>
        <translation>Fehler beim Ausführen von Diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="167"/>
        <source>Error removing existing partitions</source>
        <translation>Fehler beim Entfernen von existierenden Partitionen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="193"/>
        <source>Authentication cancelled</source>
        <translation>Authentifizierung abgebrochen</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="196"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fehler beim Ausführen von authopen, um Zugriff auf Geräte zu erhalten &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="197"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>I don&apos;t use Mac OS, I would need help here. Unfinished translation:

Bitte stellen Sie sicher, dass &apos;Raspberry Pi Imager&apos; Zugriff auf &apos;removable volumes&apos; in privacy settings hat (unter &apos;files and folders&apos;. Sie können ihm auch &apos;full disk access&apos; geben).</translatorcomment>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="218"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Speichergerät &apos;%1&apos; kann nicht geöffnet werden.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="260"/>
        <source>discarding existing data on drive</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="280"/>
        <source>zeroing out first and last MB of drive</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="286"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Schreibfehler während dem Löschen des MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="298"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Fehler beim Löschen des letzten Teiles der Speicherkarte.&lt;br&gt;Die Speicherkarte könnte mit einer falschen Größe beworben sein (möglicherweise Betrug).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="356"/>
        <source>starting download</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="740"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Fehler beim Lesen vom Speicher.&lt;br&gt;Die SD-Karte könnte kaputt sein.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="812"/>
        <source>Waiting for FAT partition to be mounted</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="860"/>
        <source>Error mounting FAT32 partition</source>
        <translation type="unfinished">Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="882"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation type="unfinished">Das Betriebssystem band die FAT32-Partition nicht ein</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="886"/>
        <source>Customizing image</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="898"/>
        <source>Error creating firstrun.sh on FAT partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="939"/>
        <source>Error writing to config.txt on FAT partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="963"/>
        <source>Error writing to cmdline.txt on FAT partition</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="398"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Zugriff verweigert-Fehler beim Schreiben auf den Datenträger.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="403"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translatorcomment>I don&apos;t use Windows either. What is &quot;Controlled Folder Access&quot; in the German version?

Controlled Folder Access sheint aktiviert zu sein. Bitte fügen Sie sowohl rpi-imager.exe als auch fat32format.exe zur Liste der erlaubten Apps hinzu und versuchen sie es erneut.</translatorcomment>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="409"/>
        <source>Error writing file to disk</source>
        <translation>Fehler beim Schreiben der Datei auf den Speicher</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="428"/>
        <source>Error downloading: %1</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="635"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Download beschädigt. Prüfsumme stimmt nicht überein</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="647"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während flushing)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="654"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="680"/>
        <source>Error writing first block (partition table)</source>
        <translation>Fehler beim Schreiben auf des ersten Blocks (Partitionstabelle)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="759"/>
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
        <location filename="../driveformatthread.cpp" line="196"/>
        <source>Error starting mkfs.fat</source>
        <translation>Fehler beim Starten von mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="206"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Fehler beim Verwenden von mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="213"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatieren ist auf dieser Platform nicht implementiert</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="182"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Die Speicherkapazität ist nicht groß genug.&lt;br&gt;Sie muss mindestens %1 GB betragen.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="188"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Die Eingabedatei ist kein gültiges Disk-Image.&lt;br&gt;Die Dateigröße%1 Bytes ist kein Vielfaches von 512 Bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="370"/>
        <source>Downloading and writing image</source>
        <translation>Image herunterladen und schreiben</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="503"/>
        <source>Select image</source>
        <translation>Image wählen</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation type="unfinished"></translation>
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
        <location filename="../MsgPopup.qml" line="96"/>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="109"/>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="122"/>
        <source>CONTINUE</source>
        <translation>WEITER</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="74"/>
        <source>Advanced options</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="91"/>
        <source>Image customization options</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="99"/>
        <source>for this session only</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="100"/>
        <source>to always use</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="112"/>
        <source>Disable overscan</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="117"/>
        <source>Set hostname:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="136"/>
        <source>Enable SSH</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="155"/>
        <source>Use password authentication</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="171"/>
        <source>Set password for &apos;pi&apos; user:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="193"/>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="208"/>
        <source>Set authorized_keys for &apos;pi&apos;:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="220"/>
        <source>Configure wifi</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="239"/>
        <source>SSID:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="248"/>
        <source>Password:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="260"/>
        <source>Show password</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="265"/>
        <source>Wifi country:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="276"/>
        <source>Set locale settings</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="286"/>
        <source>Time zone:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="296"/>
        <source>Keyboard layout:</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="306"/>
        <source>Skip first-run wizard</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="313"/>
        <source>Persistent settings</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="321"/>
        <source>Play sound when finished</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="325"/>
        <source>Eject media when finished</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="329"/>
        <source>Enable telemetry</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="342"/>
        <source>SAVE</source>
        <translation type="unfinished"></translation>
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
        <location filename="../UseSavedSettingsPopup.qml" line="71"/>
        <source>Warning: advanced settings set</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="85"/>
        <source>Would you like to apply the image customization settings saved earlier?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="94"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="106"/>
        <source>YES</source>
        <translation type="unfinished">JA</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="118"/>
        <source>EDIT SETTINGS</source>
        <translation type="unfinished"></translation>
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
        <location filename="../main.qml" line="98"/>
        <location filename="../main.qml" line="314"/>
        <source>Operating System</source>
        <translation>Betriebssystem</translation>
    </message>
    <message>
        <location filename="../main.qml" line="110"/>
        <source>CHOOSE OS</source>
        <translation>OS WÄHLEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="125"/>
        <source>Select this button to change the operating system</source>
        <translation>Klicke auf diesen Knopf, um das Betriebssystem zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="138"/>
        <location filename="../main.qml" line="598"/>
        <source>Storage</source>
        <translation>SD-Karte</translation>
    </message>
    <message>
        <location filename="../main.qml" line="150"/>
        <location filename="../main.qml" line="908"/>
        <source>CHOOSE STORAGE</source>
        <translation>SD-KARTE WÄHLEN</translation>
    </message>
    <message>
        <source>Select this button to change the destination SD card</source>
        <translation type="vanished">Klicke auf diesen Knopf, um die Zeil-SD-Karte zu ändern</translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>WRITE</source>
        <translation>SCHREIBEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="185"/>
        <source>Select this button to start writing the image</source>
        <translation>Klicke auf diesen Knopf, um mit dem Schreiben zu beginnen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="230"/>
        <source>CANCEL WRITE</source>
        <translation>SCHREIBEN ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="233"/>
        <location filename="../main.qml" line="846"/>
        <source>Cancelling...</source>
        <translation>Abbrechen...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="245"/>
        <source>CANCEL VERIFY</source>
        <translation>VERIFIZIERUNG ABBRECHEN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="248"/>
        <location filename="../main.qml" line="869"/>
        <location filename="../main.qml" line="926"/>
        <source>Finalizing...</source>
        <translation>Finalisieren...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="414"/>
        <location filename="../main.qml" line="902"/>
        <source>Erase</source>
        <translation>Löschen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="415"/>
        <source>Format card as FAT32</source>
        <translation>Karte als FAT32 formatieren</translation>
    </message>
    <message>
        <location filename="../main.qml" line="422"/>
        <source>Use custom</source>
        <translation>Eigenes Image</translation>
    </message>
    <message>
        <location filename="../main.qml" line="423"/>
        <source>Select a custom .img from your computer</source>
        <translation>Wählen Sie eine eigene .img-Datei von Ihrem Computer</translation>
    </message>
    <message>
        <location filename="../main.qml" line="374"/>
        <source>Back</source>
        <translation>Zurück</translation>
    </message>
    <message>
        <location filename="../main.qml" line="163"/>
        <source>Select this button to change the destination storage device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="375"/>
        <source>Go back to main menu</source>
        <translation>Zurück zum Hauptmenü</translation>
    </message>
    <message>
        <location filename="../main.qml" line="485"/>
        <source>Released: %1</source>
        <translation>Veröffentlicht: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="488"/>
        <source>Cached on your computer</source>
        <translation>Auf Ihrem Computer zwischengespeichert</translation>
    </message>
    <message>
        <location filename="../main.qml" line="490"/>
        <source>Local file</source>
        <translation>Lokale Datei</translation>
    </message>
    <message>
        <location filename="../main.qml" line="492"/>
        <source>Online - %1 GB download</source>
        <translation>Online - %1 GB Download</translation>
    </message>
    <message>
        <location filename="../main.qml" line="649"/>
        <location filename="../main.qml" line="701"/>
        <location filename="../main.qml" line="707"/>
        <source>Mounted as %1</source>
        <translation>Als %1 eingebunden</translation>
    </message>
    <message>
        <location filename="../main.qml" line="703"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[SCHREIBGESCHÜTZT]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="745"/>
        <source>Are you sure you want to quit?</source>
        <translation>Sind Sie sicher, dass Sie beenden möchten?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="746"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Der Raspberry Pi Imager ist noch beschäftigt. &lt;br&gt;Möchten Sie wirklich beenden?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="757"/>
        <source>Warning</source>
        <translation>Warnung</translation>
    </message>
    <message>
        <location filename="../main.qml" line="763"/>
        <source>Preparing to write...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="787"/>
        <source>Update available</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="788"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="849"/>
        <source>Writing... %1%</source>
        <translation>Schreiben... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="776"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Alle vorhandenen Daten auf &apos;%1&apos; werden gelöscht.&lt;br&gt;Möchten sie wirklich fortfahren?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="828"/>
        <source>Error downloading OS list from Internet</source>
        <translation>Fehler beim Herunterladen der Betriebssystemsliste aus dem Internet</translation>
    </message>
    <message>
        <location filename="../main.qml" line="872"/>
        <source>Verifying... %1%</source>
        <translation>Verifizierung... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="879"/>
        <source>Preparing to write... (%1)</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../main.qml" line="894"/>
        <source>Error</source>
        <translation>Fehler</translation>
    </message>
    <message>
        <location filename="../main.qml" line="901"/>
        <source>Write Successful</source>
        <translation>Schreiben erfolgreich</translation>
    </message>
    <message>
        <location filename="../main.qml" line="903"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde geleert&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="905"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde auf&lt;b&gt;%2&lt;/b&gt; geschrieben&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="942"/>
        <source>Error parsing os_list.json</source>
        <translation>Fehler beim Parsen von os_list.json</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1043"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Verbinden Sie zuerst einen USB-Stick mit Images.&lt;br&gt;Die Images müssen sich im Wurzelverzeichnes des USB-Sticks befinden.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1058"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>Die Speicherkarte ist schreibgeschützt.&lt;br&gt;Drücken Sie den Schutzschalter auf der linken Seite nach oben, und versuchen Sie es erneut.</translation>
    </message>
</context>
</TS>
