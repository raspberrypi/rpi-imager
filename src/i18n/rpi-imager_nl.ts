<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="nl_NL">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Fout bij uitpakken archiefbestand: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Fout bij mounten FAT32 partitie</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Besturingssysteem heeft FAT32 partitie niet gemount</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Fout bij openen map &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Fout bij lezen van SD kaart.&lt;br&gt;Kaart is mogelijk defect.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Verificatie mislukt. De gegevens die op de SD kaart staan wijken af van wat er naar geschreven is.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>unmounten van schijf</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>openen van opslag</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Fout bij uitvoeren diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Fout bij verwijderen bestaande partities</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Authenticatie geannuleerd</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Fout bij uitvoeren authopen: &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Gelieve te controlleren of &apos;Raspberry Pi Imager&apos; toegang heeft tot &apos;verwijderbare volumes&apos; in de privacy instellingen (onder &apos;bestanden en mappen&apos; of anders via &apos;volledige schijftoegang&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Fout bij openen opslagapparaat &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>wissen bestaande gegevens</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>wissen eerste en laatste MB van opslag</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Fout bij wissen MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Fout bij wissen laatste deel van de SD kaart.&lt;br&gt;Kaart geeft mogelijk onjuiste capaciteit aan (mogelijk counterfeit).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>beginnen met downloaden</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Fout bij downloaden: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Toegang geweigerd bij het schrijven naar opslag.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Fout bij schrijven naar opslag</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Fout bij schrijven naar opslag (tijdens flushen)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Fout bij schrijven naar opslag (tijdens fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Fout bij schrijven naar eerste deel van kaart (partitie tabel)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Fout bij lezen van SD kaart.&lt;br&gt;Kaart is mogelijk defect.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verificatie mislukt. De gegevens die op de SD kaart staan wijken af van wat er naar geschreven is.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Bezig met aanpassen besturingssysteem</translation>
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
        <source>Error partitioning: %1</source>
        <translation>Fout bij partitioneren: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Fout bij formatteren (via udisks2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatteren is niet geimplementeerd op dit besturingssysteem</translation>
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
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation type="unfinished">Opslagapparaat</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="unfinished">Mounted op %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[ALLEEN LEZEN]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">SD kaart is tegen schrijven beveiligd.&lt;br&gt;Druk het schuifje aan de linkerkant van de SD kaart omhoog, en probeer nogmaals.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">KIES MODEL</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Raspberry Pi Model</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>Opslagcapaciteit niet groot genoeg.&lt;br&gt;Deze dient minimaal %1 GB te zijn.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Invoerbestand is geen disk image.&lt;br&gt;Bestandsgrootte %1 bytes is geen veelvoud van 512 bytes.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Downloaden en schrijven van image</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Selecteer image</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Wilt u het wifi wachtwoord van het systeem overnemen?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>openen image</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Fout bij openen image bestand</translation>
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
        <translation>Nee</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>Ja</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>VERDER GAAN</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>AFSLUITEN</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation type="unfinished">Besturingssysteem</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Terug</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Terug naar hoofdmenu</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Release datum: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">Opgeslagen op computer</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Lokaal bestand</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Online %1 GB download</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Sluit eerst een USB stick met images aan.&lt;br&gt;De images moeten in de hoofdmap van de USB stick staan.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Hostnaam:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Gebruikersnaam en wachtwoord instellen</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Gebruikersnaam:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Wachtwoord:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Wifi instellen</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">Verborgen SSID</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">Wifi land:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Regio instellingen</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Tijdzone:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Toetsenbord indeling:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Geluid afspelen zodra voltooid</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Media uitwerpen zodra voltooid</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Telemetry inschakelen</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>OS aanpassen</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Algemeen</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Services</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Opties</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>OPSLAAN</translation>
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
        <translation type="unfinished">SSH inschakelen</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Gebruik wachtwoord authenticatie</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Gebruik uitsluitend public-key authenticatie</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">authorized_keys voor &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">START SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Paste your SSH public key here.
Supported formats: ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates
Example: ssh-rsa AAAAB3NzaC1yc2E... user@hostname</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Wilt u uw eigen instellingen op het OS toepassen?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>Nee</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>Nee, wis instellingen</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>Ja</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>Aanpassen</translation>
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
        <translation>Raspberry Pi Model</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Kies deze knop om het Raspberry Pi model te selecteren</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Besturingssysteem</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>KIES OS</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Kies deze knop om een besturingssysteem te kiezen</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Opslagapparaat</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>KIES OPSLAG</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Klik op deze knop om het opslagapparaat te wijzigen</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>Annuleer schrijven</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Annuleren...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>ANNULEER VERIFICATIE</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Afronden...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Volgende</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Kies deze knop om te beginnen met het schrijven van de image</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Custom repository in gebruik: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Toetsenbord navigatie: &lt;tab&gt; ga naar volgende knop &lt;spatie&gt; druk op knop/selecteer item &lt;pijltje omhoog/omlaag&gt; ga omhoog/omlaag in lijsten</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Taal: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Toetsenbord: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Weet u zeker dat u wilt afsluiten?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager is nog niet klaar.&lt;br&gt;Weet u zeker dat u wilt afsluiten?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Waarschuwing</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Voorbereiden...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Alle bestaande gegevens op &apos;%1&apos; zullen verwijderd worden.&lt;br&gt;Weet u zeker dat u door wilt gaan?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Update beschikbaar</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Er is een nieuwere versie van Imager beschikbaar.&lt;br&gt;Wilt u de website bezoeken om deze te downloaden?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Schrijven... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Verifiëren... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Voorbereiden... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Fout</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Klaar met schrijven</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Wissen</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; is gewist&lt;br&gt;&lt;br&gt;U kunt nu de SD kaart uit de lezer halen</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; is geschreven naar &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;U kunt nu de SD kaart uit de lezer halen</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formatteer kaart als FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Gebruik eigen bestand</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Selecteer een eigen .img bestand</translation>
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
