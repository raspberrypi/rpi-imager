<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ca_ES">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="197"/>
        <location filename="../downloadextractthread.cpp" line="386"/>
        <source>Error extracting archive: %1</source>
        <translation>S&apos;ha produït un error en extreure l&apos;arxiu: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="262"/>
        <source>Error mounting FAT32 partition</source>
        <translation>S&apos;ha produït un error en muntar la partició FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="282"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>El sistema operatiu no ha muntat la partició FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="305"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>S&apos;ha produït un error en canviar al directori «%1»</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="119"/>
        <source>unmounting drive</source>
        <translation>S&apos;està desmuntant el dispositiu</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="139"/>
        <source>opening drive</source>
        <translation>S&apos;està obrint la unitat</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="167"/>
        <source>Error running diskpart: %1</source>
        <translation>S&apos;ha produït un error en executar «diskpart»: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="188"/>
        <source>Error removing existing partitions</source>
        <translation>S&apos;ha produït un error en eliminar les particions existents.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="214"/>
        <source>Authentication cancelled</source>
        <translation>S&apos;ha cancel·lat l&apos;autenticació</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>S&apos;ha produït un error en executar «authopen» per a obtenir l&apos;accés al dispositiu de disc «%1»</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="218"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Verifiqueu si el «Raspberry Pi Imager» té accés als «volums extraïbles» des de la configuració de privacitat (sota «fitxers i carpetes» o doneu-li «accés complet al disc»)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="240"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>No s&apos;ha pogut obrir el dispositiu d&apos;emmagatzematge «%1»</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="282"/>
        <source>discarding existing data on drive</source>
        <translation>S&apos;estan descartant les dades existents a la unitat</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="302"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>S&apos;està esborrant amb zeros el primer i l&apos;últim MB de la unitat</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="308"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>S&apos;ha produït un error en esborrar amb zeros l&apos;«MBR».</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="320"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>S&apos;ha produït un error d&apos;escriptura en esborrar amb zeros l&apos;última part de la targeta.&lt;br&gt;La targeta podria estar indicant una capacitat errònia (possible falsificació)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="409"/>
        <source>starting download</source>
        <translation>S&apos;està iniciant la baixada</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="469"/>
        <source>Error downloading: %1</source>
        <translation>S&apos;ha produït un error en la baixada: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="666"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>S&apos;ha produït un error d&apos;accés denegat en escriure el fitxer al disc.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="671"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>L&apos;opció «Controla l&apos;accés de la carpeta» de la Seguretat del Windows sembla que està activada. Afegiu els executables «rpi-imager.exe» i «fat32format.exe» a la llista d&apos;aplicacions permeses i torneu-ho a provar.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="678"/>
        <source>Error writing file to disk</source>
        <translation>S&apos;ha produït un error en escriure el fitxer al disc</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="700"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>La baixada està corrompuda. El «hash» no coincideix</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="712"/>
        <location filename="../downloadthread.cpp" line="764"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>S&apos;ha produït un error en escriure a l&apos;emmagatzematge (procés: Flushing)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="719"/>
        <location filename="../downloadthread.cpp" line="771"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>S&apos;ha produït un error en escriure a l&apos;emmagatzematge (procés: fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="754"/>
        <source>Error writing first block (partition table)</source>
        <translation>S&apos;ha produït un error en escriure el primer bloc (taula de particions)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="829"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>S&apos;ha produït un error en llegir l&apos;emmagatzematge.&lt;br&gt;És possible que la targeta SD estigui malmesa.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="848"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Ha fallat la verificació de l&apos;escriptura. El contingut de la targeta SD és diferent del que s&apos;hi ha escrit.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="901"/>
        <source>Customizing image</source>
        <translation>S&apos;està personalitzant la imatge</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>S&apos;ha produït un error durant la partició: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>S&apos;ha produït un error en iniciar «fat32format»</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>S&apos;ha produït un error en executar «fat32format»: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>S&apos;ha produït un error en determinar la lletra de la nova unitat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>El dispositiu no és vàlid: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>S&apos;ha produït un error en formatar (a través d&apos;«udisks2»)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>S&apos;ha produït un error en iniciar «sfdisk»</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>No s&apos;ha pogut crear la partició FAT %1 esperada</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>S&apos;ha produït un error en iniciar «mkfs.fat»</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>S&apos;ha produït un error en executar «mkfs.fat»: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Aquesta plataforma no té implementada la formatació</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <location filename="../DstPopup.qml" line="24"/>
        <source>Storage</source>
        <translation type="unfinished">Emmagatzematge</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="38"/>
        <source>No storage devices found</source>
        <translation type="unfinished">No s&apos;ha trobat cap dispositiu d&apos;emmagatzematge</translation>
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
        <translation type="unfinished">Muntat com a %1</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="139"/>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="154"/>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[PROTEGIT CONTRA ESCRIPTURA]</translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="156"/>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../DstPopup.qml" line="197"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">La targeta SD està protegida contra escriptura.&lt;br&gt;Accioneu l&apos;interruptor del costat esquerre de la targeta SD per tal que quedi posicionat a la part superior i torneu-ho a provar.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <location filename="../hwlistmodel.cpp" line="111"/>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">TRIA LA PLACA</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <location filename="../HwPopup.qml" line="27"/>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Placa Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="301"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>La capacitat de l&apos;emmagatzematge no és suficient.&lt;br&gt;Ha de ser de %1 GB com a mínim.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="307"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>El fitxer d&apos;entrada no és una imatge de disc vàlida.&lt;br&gt;La mida del fitxer és de %1 bytes, que no és múltiple de 512 bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="716"/>
        <source>Downloading and writing image</source>
        <translation>S&apos;està baixant i escrivint la imatge</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="814"/>
        <source>Select image</source>
        <translation>Selecciona una imatge</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="989"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>S&apos;ha produït un error en sincronitzar l&apos;hora. Torneu-ho a provar en 3 segons.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1001"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>El commutador d&apos;Ethernet té l&apos;STP activat. L&apos;obtenció de la IP pot trigar molt.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1212"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Voleu emplenar la contrasenya del wifi des del clauer del sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="36"/>
        <source>opening image file</source>
        <translation>S&apos;està obrint el fitxer de la imatge</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="41"/>
        <source>Error opening image file</source>
        <translation>S&apos;ha produït un error en obrir el fitxer de la imatge</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="52"/>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="61"/>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="70"/>
        <source>CONTINUE</source>
        <translation>CONTINUA</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="78"/>
        <source>QUIT</source>
        <translation>SURT</translation>
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
        <translation type="unfinished">Sistema operatiu</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="92"/>
        <source>Back</source>
        <translation type="unfinished">Enrere</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="93"/>
        <source>Go back to main menu</source>
        <translation type="unfinished">Torna al menú principal</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="250"/>
        <source>Released: %1</source>
        <translation type="unfinished">Llançat el: %1</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="260"/>
        <source>Cached on your computer</source>
        <translation type="unfinished">A la memòria cau de l&apos;ordinador</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="262"/>
        <source>Local file</source>
        <translation type="unfinished">Fitxer local</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="263"/>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Disponible en línia (%1 GB)</translation>
    </message>
    <message>
        <location filename="../OSPopup.qml" line="349"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Connecteu una memòria USB que contingui primer imatges.&lt;br&gt;Les imatges s&apos;han de trobar a la carpeta arrel de la memòria.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="40"/>
        <source>Set hostname:</source>
        <translation type="unfinished">Defineix un nom de la màquina (hostname):</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="68"/>
        <source>Set username and password</source>
        <translation type="unfinished">Defineix el nom d&apos;usuari i contrasenya</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="81"/>
        <source>Username:</source>
        <translation type="unfinished">Nom d&apos;usuari:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="106"/>
        <location filename="../OptionsGeneralTab.qml" line="183"/>
        <source>Password:</source>
        <translation type="unfinished">Contrasenya:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="147"/>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Configura la wifi</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="161"/>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="212"/>
        <source>Show password</source>
        <translation type="unfinished">Mostra la contrasenya</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="223"/>
        <source>Hidden SSID</source>
        <translation type="unfinished">SSID oculta</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="234"/>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">País del wifi:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="252"/>
        <source>Set locale settings</source>
        <translation type="unfinished">Estableix la configuració regional</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="256"/>
        <source>Time zone:</source>
        <translation type="unfinished">Fus horari:</translation>
    </message>
    <message>
        <location filename="../OptionsGeneralTab.qml" line="275"/>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Disposició del teclat:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <location filename="../OptionsMiscTab.qml" line="23"/>
        <source>Play sound when finished</source>
        <translation type="unfinished">Fes un so quan acabi</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="27"/>
        <source>Eject media when finished</source>
        <translation type="unfinished">Expulsa el mitjà quan acabi</translation>
    </message>
    <message>
        <location filename="../OptionsMiscTab.qml" line="31"/>
        <source>Enable telemetry</source>
        <translation type="unfinished">Activa la telemetria</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="28"/>
        <source>OS Customization</source>
        <translation>Personalització del SO</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="65"/>
        <source>General</source>
        <translation>General</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="74"/>
        <source>Services</source>
        <translation>Serveis</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="80"/>
        <source>Options</source>
        <translation>Opcions</translation>
    </message>
    <message>
        <source>Set hostname:</source>
        <translation type="vanished">Defineix un nom de la màquina (hostname):</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="vanished">Defineix el nom d&apos;usuari i contrasenya</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="vanished">Nom d&apos;usuari:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="vanished">Contrasenya:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="vanished">Configura la wifi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="vanished">SSID:</translation>
    </message>
    <message>
        <source>Show password</source>
        <translation type="vanished">Mostra la contrasenya</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="vanished">SSID oculta</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="vanished">País del wifi:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="vanished">Estableix la configuració regional</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="vanished">Fus horari:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="vanished">Disposició del teclat:</translation>
    </message>
    <message>
        <source>Enable SSH</source>
        <translation type="vanished">Activa el protocol SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="vanished">Utilitza l&apos;autenticació de contrasenya</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="vanished">Permet només l&apos;autenticació de claus públiques</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="vanished">Establiu «authorized_keys» per a l&apos;usuari «%1»:</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="vanished">EXECUTA SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Play sound when finished</source>
        <translation type="vanished">Fes un so quan acabi</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="vanished">Expulsa el mitjà quan acabi</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="vanished">Activa la telemetria</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="123"/>
        <source>SAVE</source>
        <translation>DESA</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <location filename="../OptionsServicesTab.qml" line="36"/>
        <source>Enable SSH</source>
        <translation type="unfinished">Activa el protocol SSH</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="52"/>
        <source>Use password authentication</source>
        <translation type="unfinished">Utilitza l&apos;autenticació de contrasenya</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="63"/>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Permet només l&apos;autenticació de claus públiques</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="74"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Establiu «authorized_keys» per a l&apos;usuari «%1»:</translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="121"/>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="../OptionsServicesTab.qml" line="140"/>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">EXECUTA SSH-KEYGEN</translation>
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
        <translation>Lector de targetes SD intern</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Use OS customization?</source>
        <translation type="vanished">Voleu personalitzar el SO?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="41"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Voleu aplicar la configuració personalitzada del SO?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="82"/>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="63"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NO, ESBORRA LA CONFIGURACIÓ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="73"/>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="50"/>
        <source>EDIT SETTINGS</source>
        <translation>EDITA LA CONFIGURACIÓ</translation>
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
        <translation>Placa Raspberry Pi</translation>
    </message>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="vanished">TRIA LA PLACA</translation>
    </message>
    <message>
        <location filename="../main.qml" line="144"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Seleccioneu aquest botó per a escollir la placa Raspberry Pi objectiu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="158"/>
        <source>Operating System</source>
        <translation>Sistema operatiu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="169"/>
        <location filename="../main.qml" line="446"/>
        <source>CHOOSE OS</source>
        <translation>TRIA EL SO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="182"/>
        <source>Select this button to change the operating system</source>
        <translation>Seleccioneu aquest botó si voleu canviar el sistema operatiu</translation>
    </message>
    <message>
        <location filename="../main.qml" line="196"/>
        <source>Storage</source>
        <translation>Emmagatzematge</translation>
    </message>
    <message>
        <location filename="../main.qml" line="331"/>
        <source>Network not ready yet</source>
        <translation>La xarxa encara no és a punt</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="vanished">No s&apos;ha trobat cap dispositiu d&apos;emmagatzematge</translation>
    </message>
    <message>
        <location filename="../main.qml" line="207"/>
        <location filename="../main.qml" line="668"/>
        <source>CHOOSE STORAGE</source>
        <translation>TRIA L&apos;EMMAGATZEMATGE</translation>
    </message>
    <message>
        <location filename="../main.qml" line="221"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Seleccioneu aquest botó per a canviar la destinació del dispositiu d&apos;emmagatzematge</translation>
    </message>
    <message>
        <location filename="../main.qml" line="266"/>
        <source>CANCEL WRITE</source>
        <translation>CANCEL·LA L&apos;ESCRIPTURA</translation>
    </message>
    <message>
        <location filename="../main.qml" line="269"/>
        <location filename="../main.qml" line="591"/>
        <source>Cancelling...</source>
        <translation>S&apos;està cancel·lant...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="281"/>
        <source>CANCEL VERIFY</source>
        <translation>CANCEL·LA LA VERIFICACIÓ</translation>
    </message>
    <message>
        <location filename="../main.qml" line="284"/>
        <location filename="../main.qml" line="614"/>
        <location filename="../main.qml" line="687"/>
        <source>Finalizing...</source>
        <translation>S&apos;està finalitzant...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="293"/>
        <source>Next</source>
        <translation>Següent</translation>
    </message>
    <message>
        <location filename="../main.qml" line="299"/>
        <source>Select this button to start writing the image</source>
        <translation>Seleccioneu aquest botó per a començar l&apos;escriptura de la imatge</translation>
    </message>
    <message>
        <location filename="../main.qml" line="321"/>
        <source>Using custom repository: %1</source>
        <translation>S&apos;està usant el repositori personalitzat: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="340"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navegació per teclat: &lt;tab&gt; navega al botó següent &lt;espai&gt; prem el botó o selecciona l&apos;element &lt;fletxa amunt o avall&gt; desplaçament per les llistes</translation>
    </message>
    <message>
        <location filename="../main.qml" line="361"/>
        <source>Language: </source>
        <translation>Idioma: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="384"/>
        <source>Keyboard: </source>
        <translation>Teclat: </translation>
    </message>
    <message>
        <source>[ All ]</source>
        <translation type="vanished">[ TOT ]</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="vanished">Enrere</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="vanished">Torna al menú principal</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="vanished">Llançat el: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="vanished">A la memòria cau de l&apos;ordinador</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="vanished">Fitxer local</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="vanished">Disponible en línia (%1 GB)</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation type="vanished">Muntat com a %1</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="vanished">[PROTEGIT CONTRA ESCRIPTURA]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="485"/>
        <source>Are you sure you want to quit?</source>
        <translation>Esteu segur que en voleu sortir?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="486"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>El Raspberry Pi Imager està ocupat.&lt;br&gt;Esteu segur que en voleu sortir?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="497"/>
        <source>Warning</source>
        <translation>Avís</translation>
    </message>
    <message>
        <location filename="../main.qml" line="506"/>
        <source>Preparing to write...</source>
        <translation>S&apos;està preparant per a escriure...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="520"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Totes les dades existents a «%1» s&apos;esborraran.&lt;br&gt;Esteu segur que voleu continuar?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="535"/>
        <source>Update available</source>
        <translation>Hi ha una actualització disponible</translation>
    </message>
    <message>
        <location filename="../main.qml" line="536"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Hi ha una nova versió de l&apos;Imager disponible.&lt;br&gt;Voleu visitar el lloc web per baixar-la?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="594"/>
        <source>Writing... %1%</source>
        <translation>S&apos;està escrivint... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="617"/>
        <source>Verifying... %1%</source>
        <translation>S&apos;està verificant... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="624"/>
        <source>Preparing to write... (%1)</source>
        <translation>S&apos;està preparant per escriure... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="644"/>
        <source>Error</source>
        <translation>S&apos;ha produït un error</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Write Successful</source>
        <translation>S&apos;ha escrit amb èxit</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <location filename="../imagewriter.cpp" line="648"/>
        <source>Erase</source>
        <translation>Esborra</translation>
    </message>
    <message>
        <location filename="../main.qml" line="653"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>S&apos;ha esborrat &lt;b&gt;%1&lt;/b&gt;&lt;br&gt;&lt;br&gt;Ja podeu retirar la targeta SD del lector</translation>
    </message>
    <message>
        <location filename="../main.qml" line="660"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>S&apos;ha escrit el «&lt;b&gt;%1&lt;/b&gt;» a &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Ja podeu retirar la targeta SD del lector</translation>
    </message>
    <message>
        <source>Error parsing os_list.json</source>
        <translation type="vanished">S&apos;ha produït un error en analitzar os_lists.json</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="649"/>
        <source>Format card as FAT32</source>
        <translation>Formata la targeta com a FAT32</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="655"/>
        <source>Use custom</source>
        <translation>Utilitza una personalitzada</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="656"/>
        <source>Select a custom .img from your computer</source>
        <translation>Selecciona una imatge .img personalitzada de l&apos;ordinador</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="vanished">Connecteu una memòria USB que contingui primer imatges.&lt;br&gt;Les imatges s&apos;han de trobar a la carpeta arrel de la memòria.</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="vanished">La targeta SD està protegida contra escriptura.&lt;br&gt;Accioneu l&apos;interruptor del costat esquerre de la targeta SD per tal que quedi posicionat a la part superior i torneu-ho a provar.</translation>
    </message>
</context>
</TS>
