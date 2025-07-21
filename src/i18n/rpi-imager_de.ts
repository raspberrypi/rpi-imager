<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="de_DE">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Fehler beim Entpacken des Archivs: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Fehler beim Einbinden der FAT32-Partition</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Das Betriebssystem band die FAT32-Partition nicht ein</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Fehler beim Wechseln in den Ordner &quot;%1&quot;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Fehler beim Lesen vom Speicher.&lt;br&gt;Die SD-Karte könnte defekt sein.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Verifizierung fehlgeschlagen. Der Inhalt der SD-Karte weicht von dem Inhalt ab, der geschrieben werden sollte.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>Laufwerk wird ausgehängt</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>Laufwerk wird geöffnet</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Fehler beim Ausführen von Diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Fehler beim Entfernen von existierenden Partitionen</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Authentifizierung abgebrochen</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fehler beim Ausführen von authopen, um Zugriff auf Geräte zu erhalten &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translatorcomment>Not sure if current macOS has that option (or if it got moved/renamed)</translatorcomment>
        <translation type="unfinished">Bitte stellen Sie sicher, dass &apos;Raspberry Pi Imager&apos; Zugriff auf &apos;removable volumes&apos; in privacy settings hat (unter &apos;files and folders&apos;. Sie können ihm auch &apos;full disk access&apos; geben).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Speichergerät &apos;%1&apos; kann nicht geöffnet werden.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>Vorhandene Daten auf dem Medium werden gelöscht</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>Erstes und letztes Megabyte des Mediums werden überschrieben</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Schreibfehler während des Löschens des MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Fehler beim Löschen des letzten Teiles der Speicherkarte.&lt;br&gt;Die Speicherkarte könnte mit einer falschen Größe beworben sein (möglicherweise Betrug).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>Download wird gestartet</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Fehler beim Herunterladen: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Zugriff verweigert-Fehler beim Schreiben auf den Datenträger.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Fehler beim Schreiben der Datei auf den Speicher</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während flushing)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fehler beim Schreiben auf den Speicher (während fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Fehler beim Schreiben des ersten Blocks (Partitionstabelle)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Fehler beim Lesen vom Speicher.&lt;br&gt;Die SD-Karte könnte defekt sein.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verifizierung fehlgeschlagen. Der Inhalt der SD-Karte weicht von dem Inhalt ab, der geschrieben werden sollte.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Image modifizieren</translation>
    </message>
    <message>
        <source>Cached file is corrupt. SHA256 hash does not match expected value.&lt;br&gt;The cache file will be removed and the download will restart.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Local file is corrupt or has incorrect SHA256 hash.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Download appears to be corrupt. SHA256 hash does not match.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2&lt;br&gt;Please check your network connection and try again.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add rpi-imager.exe to the list of allowed apps and try again.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Fehler beim Formatieren (mit udisks2)</translation>
    </message>
    <message>
        <source>Error opening device for formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error writing to device during formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error seeking on device during formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Invalid parameters for formatting</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Insufficient space on device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Unknown formatting error</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Cannot format device: insufficient permissions and udisks2 not available</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation>SD-Karte</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation>Keine SD-Karte gefunden</translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation>Systemlaufwerke ausschließen</translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation>Gigabytes</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation>Als %1 eingebunden</translation>
    </message>
    <message>
        <source>GB</source>
        <translation>GB</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation>[SCHREIBGESCHÜTZT]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation>SYSTEM</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>Die Speicherkarte ist schreibgeschützt.&lt;br&gt;Schieben Sie den Schutzschalter auf der linken Seite nach oben, und versuchen Sie es erneut.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation>MODELL WÄHLEN</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Pi Modell</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Die Speicherkapazität ist nicht groß genug.&lt;br&gt;Sie muss mindestens %1 GB betragen.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Die Eingabedatei ist kein gültiges Disk-Image.&lt;br&gt;Die Dateigröße%1 Bytes ist kein Vielfaches von 512 Bytes.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Image herunterladen und schreiben</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Image wählen</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Fehler beim Synchronisieren der Zeit. Neuer Versuch in 3 Sekunden</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>STP ist auf Ihrem Ethernet-Switch aktiviert. Das Abrufen der IP wird lange dauern.</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Möchten Sie das Wifi-Passwort aus dem System-Schlüsselbund vorab ausfüllen?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>Abbilddatei wird geöffnet</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Fehler beim Öffnen der Imagedatei</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error reading from image file</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Error writing to device</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Failed to read complete image file</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>WEITER</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>BEENDEN</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation>Empfohlen</translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation>Betriebssystem (OS)</translation>
    </message>
    <message>
        <source>Back</source>
        <translation>Zurück</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation>Zurück zum Hauptmenü</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation>Veröffentlicht: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation>Auf Ihrem Computer zwischengespeichert</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation>Lokale Datei</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation>Online - %1 GB Download</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Verbinden Sie zuerst einen USB-Stick mit Images.&lt;br&gt;Die Images müssen sich im Wurzelverzeichnes des USB-Sticks befinden.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation>Hostname:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation>Benutzername und Passwort festlegen</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation>Benutzername:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation>Passwort:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation>Wifi einrichten</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation>Verborgene SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation>Wifi-Land:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation>Spracheinstellungen festlegen</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation>Zeitzone:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation>Tastaturlayout:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation>Tonsignal nach Beenden abspielen</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation>Medien nach Beenden auswerfen</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation>Telemetrie aktivieren</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>OS Anpassungen</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Allgemein</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Dienste</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Optionen</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>SPEICHERN</translation>
    </message>
    <message>
        <source>CANCEL</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in General and Services tabs</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in General tab</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Please fix validation errors in Services tab</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation>SSH aktivieren</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation>Passwort zur Authentifizierung verwenden</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation>Authentifizierung via Public-Key</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>authorized_keys für &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation>Schlüssel löschen</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation>SSH-KEYGEN ausführen</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation>Schlüssel hinzufügen</translation>
    </message>
    <message>
        <source>Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Paste your SSH public key here.
Supported formats: ssh-rsa, ssh-ed25519, ssh-dss, ecdsa-sha2-nistp, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates
Example: ssh-rsa AAAAB3NzaC1yc2E... user@hostname</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Möchten Sie die vorher festgelegten OS Anpassungen anwenden?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NEIN</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NEIN, EINSTELLUNGEN LÖSCHEN</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>JA</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>EINSTELLUNGEN BEARBEITEN</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v%1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Raspberry Pi Modell</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Klicken Sie auf diesen Knopf, um den gewünschten Raspberry Pi auszuwählen</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Betriebssystem (OS)</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>OS WÄHLEN</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Klicken Sie auf diesen Knopf, um das Betriebssystem zu ändern</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>SD-Karte</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>Netzwerk noch nicht bereit</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>SD-KARTE WÄHLEN</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Klicken Sie auf diesen Knopf, um das Ziel-Speichermedium zu ändern</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>SCHREIBEN ABBRECHEN</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Abbrechen...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>VERIFIZIERUNG ABBRECHEN</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Finalisieren...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Weiter</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Klicken Sie auf diesen Knopf, um mit dem Schreiben zu beginnen</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Verwende benutzerdefiniertes Repository: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Tastaturnavigation: &lt;Tab&gt; zum nächsten Knopf navigieren &lt;Leertaste&gt; Knopf drücken/Element auswählen &lt;Pfeil hoch/runter&gt; in Listen nach oben/unten gehen</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Sprache: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Tastatur: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Sind Sie sicher, dass Sie beenden möchten?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Der Raspberry Pi Imager ist noch beschäftigt. &lt;br&gt;Möchten Sie wirklich beenden?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Warnung</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Schreiben wird vorbereitet...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Alle vorhandenen Daten auf &apos;%1&apos; werden gelöscht.&lt;br&gt;Möchten Sie wirklich fortfahren?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Update verfügbar</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Eine neuere Version von Imager ist verfügbar.&lt;br&gt;Möchten Sie die Webseite besuchen, um das Update herunterzuladen?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Schreiben... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Verifizieren... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Schreiben wird vorbereitet... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Fehler</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Schreiben erfolgreich</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Löschen</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde geleert&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; wurde auf &lt;b&gt;%2&lt;/b&gt; geschrieben&lt;br&gt;&lt;br&gt;Sie können die SD-Karte nun aus dem Lesegerät entfernen</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Karte als FAT32 formatieren</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Eigenes Image</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Wählen Sie eine eigene .img-Datei von Ihrem Computer</translation>
    </message>
    <message>
        <source>SKIP CACHE VERIFICATION</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Starting download...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Verifying cached file... %1%</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Verifying cached file...</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Starting write...</source>
        <translation type="unfinished"></translation>
    </message>
</context>
</TS>
