<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ca_ES">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>S&apos;ha produït una error en extreure l&apos;arxiu: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>S&apos;ha produït una error en muntar la partició FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>El sistema operatiu no ha muntat la partició FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>S&apos;ha produït una error en canviar al directori «%1»</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>S&apos;ha produït una error en llegir l&apos;emmagatzematge.&lt;br&gt;És possible que la targeta SD estigui malmesa.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Ha fallat la verificació de l&apos;escriptura. El contingut de la targeta SD és diferent del que s&apos;hi ha escrit.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>S&apos;està desmuntant el dispositiu</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>S&apos;està obrint la unitat</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>S&apos;ha produït una error en executar «diskpart»: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>S&apos;ha produït una error en eliminar les particions existents.</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>S&apos;ha cancel·lat l&apos;autenticació</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>S&apos;ha produït una error en executar «authopen» per a obtenir l&apos;accés al dispositiu de disc «%1»</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Verifiqueu si el «Raspberry Pi Imager» té accés als «volums extraïbles» en la configuració de privacitat (sota «fitxers i carpetes» o doneu-li «accés complet al disc»)</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>No s&apos;ha pogut obrir el dispositiu d&apos;emmagatzematge «%1»</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>S&apos;estan descartant les dades existents a la unitat</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>S&apos;està esborrant amb zeros el primer i l&apos;últim MB de la unitat</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>S&apos;ha produït una error en esborrar amb zeros l&apos;«MBR».</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>S&apos;ha produït una error d&apos;escriptura en esborrar amb zeros l&apos;última part de la targeta.&lt;br&gt;La targeta podria estar indicant una capacitat errònia (possible falsificació)</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>S&apos;està iniciant la baixada</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>S&apos;ha produït una error en la baixada: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>S&apos;ha produït una error d&apos;accés denegat en escriure el fitxer al disc.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>S&apos;ha produït una error en escriure el fitxer al disc</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>S&apos;ha produït una error en escriure a l&apos;emmagatzematge (procés: Flushing)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>S&apos;ha produït una error en escriure a l&apos;emmagatzematge (procés: fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>S&apos;ha produït una error en escriure el primer bloc (taula de particions)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>S&apos;ha produït una error en llegir l&apos;emmagatzematge.&lt;br&gt;És possible que la targeta SD estigui malmesa.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Ha fallat la verificació de l&apos;escriptura. El contingut de la targeta SD és diferent del que s&apos;hi ha escrit.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>S&apos;està personalitzant la imatge</translation>
    </message>
    <message>
        <source>Cached file is corrupt. SHA256 hash does not match expected value.&lt;br&gt;The cache file will be removed and the download will restart.</source>
        <translation>El fitxer en la memòria cau és malmès. El hash SHA256 no coincideix amb la valor esperada.&lt;br&gt;El fitxer s&apos;esborrarà de la memòria cau i es tornarà a baixar.</translation>
    </message>
    <message>
        <source>Local file is corrupt or has incorrect SHA256 hash.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2</source>
        <translation>El fitxer en la memòria cau és malmès o té un hash SHA256 incorrecte.&lt;br&gt;Esperat: %1&lt;br&gt;Real: %2</translation>
    </message>
    <message>
        <source>Download appears to be corrupt. SHA256 hash does not match.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2&lt;br&gt;Please check your network connection and try again.</source>
        <translation>La baixada sembla malmesa. El hash SHA256 no coincideix.&lt;br&gt;Esperat: %1&lt;br&gt;Real: %2&lt;br&gt;Comproveu la connexió a la xarxa i torneu-ho a intentar.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add rpi-imager.exe to the list of allowed apps and try again.</source>
        <translation>L&apos;accés controlat a les carpetes sembla actiu. Afegiu rpi-imager.exe a la llista d&apos;aplicacions permeses i torneu-ho a provar.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>S&apos;ha produït una error en formatar (a través d&apos;«udisks2»).</translation>
    </message>
    <message>
        <source>Error opening device for formatting</source>
        <translation>S&apos;ha produït una error en obrir el dispositiu per a formatar.</translation>
    </message>
    <message>
        <source>Error writing to device during formatting</source>
        <translation>S&apos;ha produït una error durant la formatació.</translation>
    </message>
    <message>
        <source>Error seeking on device during formatting</source>
        <translation>S&apos;ha produït una error en la cerca del dispositiu durant la formatació.</translation>
    </message>
    <message>
        <source>Invalid parameters for formatting</source>
        <translation>Paràmetres no vàlids per a la formatació.</translation>
    </message>
    <message>
        <source>Insufficient space on device</source>
        <translation>No hi ha prou espai al dispositiu.</translation>
    </message>
    <message>
        <source>Unknown formatting error</source>
        <translation>S&apos;ha produït una error desconeguda durant la formatació.</translation>
    </message>
    <message>
        <source>Cannot format device: insufficient permissions and udisks2 not available</source>
        <translation>No s&apos;ha pogut formatar el dispositiu: no hi ha prou permisos i «udisks2» no és disponible.</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation>Emmagatzematge</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation>No s&apos;ha trobat cap dispositiu d&apos;emmagatzematge</translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation>Exclou les unitats del sistema</translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation>gigabytes</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation>Muntat com a %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation>GB</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation>[PROTEGIT CONTRA L&apos;ESCRIPTURA]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation>SISTEMA</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>La targeta SD està protegida contra l&apos;escriptura.&lt;br&gt;Accioneu l'interruptor de blocatge del costat esquerre de la targeta cap amunt i torneu-ho a provar.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation>TRIA LA PLACA</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Placa Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>La capacitat de l&apos;emmagatzematge no és suficient.&lt;br&gt;Ha de ser de %1 GB com a mínim.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>El fitxer d&apos;entrada no és una imatge de disc vàlida.&lt;br&gt;La mida del fitxer és de %1 bytes, que no és múltiple de 512 bytes.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>S&apos;està baixant i escrivint la imatge</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Tria una imatge</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>S&apos;ha produït un error en sincronitzar l&apos;hora. Torneu-ho a provar en 3 segons.</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>El commutador d&apos;Ethernet té l&apos;STP activat. L&apos;obtenció de la IP pot trigar molt.</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Voleu emplenar la contrasenya del wifi des del clauer del sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>S&apos;està obrint el fitxer de la imatge</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>S&apos;ha produït una error en obrir el fitxer de la imatge</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation>S&apos;està iniciant l&apos;extracció.</translation>
    </message>
    <message>
        <source>Error reading from image file</source>
        <translation>S&apos;ha produït una error en llegir el fitxer d&apos;imatge.</translation>
    </message>
    <message>
        <source>Error writing to device</source>
        <translation>S&apos;ha produït una error en escriure al dispositiu.</translation>
    </message>
    <message>
        <source>Failed to read complete image file</source>
        <translation>No s&apos;ha pogut llegir el fitxer d&apos;imatge sencer.</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>CONTINUA</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>SURT</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation>Recomanat</translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation>Sistema operatiu</translation>
    </message>
    <message>
        <source>Back</source>
        <translation>Enrere</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation>Torna al menú principal</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation>Llançat el: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation>A la memòria cau de l&apos;ordinador</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation>Fitxer local</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation>Disponible en línia (%1 GB)</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Connecteu una memòria USB que contingui primer imatges.&lt;br&gt;Les imatges s&apos;han de trobar a la carpeta arrel de la memòria.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation>Defineix un nom de la màquina (hostname):</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation>Defineix el nom d&apos;usuari i contrasenya</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation>Nom d&apos;usuari:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation>Contrasenya:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation>Configura el wifi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation>SSID oculta</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation>País del wifi:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation>Estableix la configuració regional</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation>Fus horari:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation>Disposició del teclat:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation>Fes un so quan acabi</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation>Expulsa el mitjà quan acabi</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation>Activa la telemetria</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Personalització del SO</translation>
    </message>
    <message>
        <source>General</source>
        <translation>General</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Serveis</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Opcions</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>DESA</translation>
    </message>
    <message>
        <source>CANCEL</source>
        <translation>CANCEL·LA</translation>
    </message>
    <message>
        <source>Please fix validation errors in General and Services tabs</source>
        <translation>Corregiu les errors de validació en les pestanyes «General» i Serveis»</translation>
    </message>
    <message>
        <source>Please fix validation errors in General tab</source>
        <translation>Corregiu les errors de validació en la pestanya «General»</translation>
    </message>
    <message>
        <source>Please fix validation errors in Services tab</source>
        <translation>Corregiu les errors de validació en la pestanya «Serveis»</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation>Activa el protocol SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation>Usa l&apos;autenticació de contrasenya</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation>Permet només l&apos;autenticació de claus públiques</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>Establiu «authorized_keys» per a l&apos;usuari «%1»:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation>Suprimeix la clau</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation>EXECUTA «SSH-KEYGEN»</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation>Afegeix una clau SSH</translation>
    </message>
    <message>
        <source>Paste your SSH public key here.
Supported formats: ssh-rsa, ssh-ed25519, ssh-dss, ecdsa-sha2-nistp, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates
Example: ssh-rsa AAAAB3NzaC1yc2E... user@hostname</source>
        <translation>Enganxeu la clau SSH pública ací.
Formats vàlids: ssh-rsa, ssh-ed25519, ssh-dss, ecdsa-sha2-nistp, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com i certificats SSH
Exemple: ssh-rsa AAAAB3NzaC1yc2E... usuari@nommaquina
</translation>
    </message>
    <message>
        <source>Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ecdsa-sha2-nistp, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.</source>
        <translation>El format de la clau SSH no és vàlid. Les claus SSH han de començar amb «ssh-rsa», «ssh-ed25519», «ssh-dss», «ecdsa-sha2-nistp», «sk-ssh-ed25519@openssh.com», «sk-ecdsa-sha2-nistp256@openssh.com» o amb un certificat SSH, seguits de les dades de la clau i comentaris opcionals.</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Voleu aplicar la configuració personalitzada del SO?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NO, ESBORRA LA CONFIGURACIÓ</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>EDITA LA CONFIGURACIÓ</translation>
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
        <translation>Placa Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Seleccioneu aquest botó per a triar la placa Raspberry Pi objectiu</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Sistema operatiu</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>TRIA EL SO</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Seleccioneu aquest botó si voleu canviar el sistema operatiu</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Emmagatzematge</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>La xarxa encara no és a punt</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>TRIA L&apos;EMMAGATZEMATGE</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Seleccioneu aquest botó per a canviar la destinació del dispositiu d&apos;emmagatzematge</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>CANCEL·LA L&apos;ESCRIPTURA</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>S&apos;està cancel·lant...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>CANCEL·LA LA VERIFICACIÓ</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>S&apos;està finalitzant...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Següent</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Seleccioneu aquest botó per a començar l&apos;escriptura de la imatge</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>S&apos;està usant el repositori personalitzat: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navegació per teclat: &lt;tab&gt; navega al botó següent &lt;espai&gt; prem el botó o selecciona l&apos;element &lt;fletxa amunt o avall&gt; desplaçament per les llistes</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Idioma: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Teclat: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Esteu segur que en voleu sortir?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>El Raspberry Pi Imager és ocupat.&lt;br&gt;Esteu segur que en voleu sortir?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Avís</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>S&apos;està preparant per a escriure...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Totes les dades existents a «%1» s&apos;esborraran.&lt;br&gt;Esteu segur que voleu continuar?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Hi ha una actualització disponible</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Hi ha una nova versió de l&apos;Imager disponible.&lt;br&gt;Voleu visitar el lloc web per baixar-la?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>S&apos;està escrivint. (%1 %)</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>S&apos;està verificant. (%1 %)</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>S&apos;està preparant per escriure. (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>S&apos;ha produït una error</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>L&apos;escriptura ha reeixit.</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Esborra</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>S&apos;ha esborrat &lt;b&gt;%1&lt;/b&gt;&lt;br&gt;&lt;br&gt;Ja podeu retirar la targeta SD del lector</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>S&apos;ha escrit el «&lt;b&gt;%1&lt;/b&gt;» a &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Ja podeu retirar la targeta SD del lector</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formata la targeta com a FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Usa&apos;n una de personalitzada</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Trieu una imatge .img personalitzada de l&apos;ordinador</translation>
    </message>
    <message>
        <source>SKIP CACHE VERIFICATION</source>
        <translation>OMET LA VERIFICACIÓ DE LA MEMÒRIA CAU</translation>
    </message>
    <message>
        <source>Starting download...</source>
        <translation>S&apos;està iniciant la baixada.</translation>
    </message>
    <message>
        <source>Verifying cached file... %1%</source>
        <translation>S&apos;està verificant el fitxer en memòria cau. (%1 %)</translation>
    </message>
    <message>
        <source>Verifying cached file...</source>
        <translation>S&apos;està verificant el fitxer en memòria cau.</translation>
    </message>
    <message>
        <source>Starting write...</source>
        <translation>S&apos;està començant a escriure.</translation>
    </message>
</context>
</TS>
