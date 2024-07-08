<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="es_ES">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="196"/>
        <location filename="../downloadextractthread.cpp" line="385"/>
        <source>Error extracting archive: %1</source>
        <translation>Error extrayendo el archivo: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="261"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Error montando la partición FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="281"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>El sistema operativo no montó la partición FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="304"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Error cambiando al directorio &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="118"/>
        <source>unmounting drive</source>
        <translation>desmontando unidad</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="138"/>
        <source>opening drive</source>
        <translation>abriendo unidad</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="166"/>
        <source>Error running diskpart: %1</source>
        <translation>Error ejecutando diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="187"/>
        <source>Error removing existing partitions</source>
        <translation>Error eliminando las particiones existentes</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="213"/>
        <source>Authentication cancelled</source>
        <translation>Autenticación cancelada</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="216"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Error ejecutando authopen para acceder al dispositivo de disco &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Por favor, compruebe si &apos;Raspberry Pi Imager&apos; tiene permitido el acceso a &apos;volúmenes extraíbles&apos; en los ajustes de privacidad (en &apos;archivos y carpetas&apos; o alternativamente dele &apos;acceso total al disco&apos;).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="239"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>No se puede abrir el dispositivo de almacenamiento &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="281"/>
        <source>discarding existing data on drive</source>
        <translation>descartando datos existentes en la unidad</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="301"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>poniendo a cero el primer y el último MB de la unidad</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="307"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Error de escritura al poner a cero MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="319"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Error de escritura al intentar poner a cero la última parte de la tarjeta.&lt;br&gt;La tarjeta podría estar anunciando una capacidad incorrecta (posible falsificación).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="408"/>
        <source>starting download</source>
        <translation>iniciando descarga</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="468"/>
        <source>Error downloading: %1</source>
        <translation>Error descargando: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="665"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Error de acceso denegado escribiendo el archivo en el disco.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="670"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>El acceso controlado a carpetas parece estar activado. Añada rpi-imager.exe y fat32format.exe a la lista de aplicaciones permitidas y vuelva a intentarlo.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="677"/>
        <source>Error writing file to disk</source>
        <translation>Error escribiendo el archivo en el disco</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="699"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Descarga corrupta. El hash no coincide</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="711"/>
        <location filename="../downloadthread.cpp" line="763"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Error escribiendo en la memoria (durante la limpieza)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="718"/>
        <location filename="../downloadthread.cpp" line="770"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Error escribiendo en el almacenamiento (mientras fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="753"/>
        <source>Error writing first block (partition table)</source>
        <translation>Error escribiendo el primer bloque (tabla de particiones)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="828"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Error leyendo del almacenamiento.&lt;br&gt;La tarjeta SD puede estar rota.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="847"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Error verificando la escritura. El contenido de la tarjeta SD es diferente del que se escribió en ella.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="900"/>
        <source>Customizing image</source>
        <translation>Personalizando imagen</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Error particionando: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Error iniciando fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Error ejecutando fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Error determinando la nueva letra de unidad</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Dispositivo no válido: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Error formateando (a través de udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Error iniciando sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>El particionado no creó la partición FAT %1 esperada</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Error iniciando mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Error ejecutando mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formateo no implementado para esta plataforma</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="253"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>La capacidad de almacenamiento no es lo suficientemente grande.&lt;br&gt;Necesita ser de al menos %1 GB.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="259"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>El archivo de entrada no es una imagen de disco válida.&lt;br&gt;El tamaño del archivo %1 bytes no es múltiplo de 512 bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="654"/>
        <source>Downloading and writing image</source>
        <translation>Descargando y escribiendo imagen</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="787"/>
        <source>Select image</source>
        <translation>Seleccionar imagen</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="962"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Error sincronizando la hora. Vuelva a intentarlo en 3 segundos</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="974"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>El comutador de Ethernet tiene el STP activado. La obtención de la IP puede tardar mucho.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1185"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>¿Desea rellenar previamente la contraseña wifi desde el llavero del sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>abriendo archivo de imagen</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>Error abriendo archivo de imagen</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="107"/>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="116"/>
        <source>CONTINUE</source>
        <translation>CONTINUAR</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="124"/>
        <source>QUIT</source>
        <translation>SALIR</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="20"/>
        <source>OS Customization</source>
        <translation>Personalización del SO</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="62"/>
        <source>General</source>
        <translation>General</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="70"/>
        <source>Services</source>
        <translation>Servicios</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="73"/>
        <source>Options</source>
        <translation>Opciones</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="89"/>
        <source>Set hostname:</source>
        <translation>Establecer nombre de anfitrión:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="112"/>
        <source>Set username and password</source>
        <translation>Establecer nombre de usuario y contraseña</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="134"/>
        <source>Username:</source>
        <translation>Nombre de usuario:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="151"/>
        <location filename="../OptionsPopup.qml" line="220"/>
        <source>Password:</source>
        <translation>Contraseña:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="187"/>
        <source>Configure wireless LAN</source>
        <translation>Configurar LAN inalámbrica</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="206"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="239"/>
        <source>Show password</source>
        <translation>Mostrar contraseña</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="245"/>
        <source>Hidden SSID</source>
        <translation>SSID oculta</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="251"/>
        <source>Wireless LAN country:</source>
        <translation>País de LAN inalámbrica:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="262"/>
        <source>Set locale settings</source>
        <translation>Establecer ajustes regionales</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="272"/>
        <source>Time zone:</source>
        <translation>Zona horaria:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="282"/>
        <source>Keyboard layout:</source>
        <translation>Distribución del teclado:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="299"/>
        <source>Enable SSH</source>
        <translation>Activar SSH</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="318"/>
        <source>Use password authentication</source>
        <translation>Usar autenticación por contraseña</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="328"/>
        <source>Allow public-key authentication only</source>
        <translation>Permitir solo la autenticación de clave pública</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="346"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>Establecer authorized_keys para &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="358"/>
        <source>RUN SSH-KEYGEN</source>
        <translation>EJECUTAR SSH-KEYGEN</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="376"/>
        <source>Play sound when finished</source>
        <translation>Reproducir sonido al finalizar</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="380"/>
        <source>Eject media when finished</source>
        <translation>Expulsar soporte al finalizar</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="384"/>
        <source>Enable telemetry</source>
        <translation>Activar telemetría</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="398"/>
        <source>SAVE</source>
        <translation>GUARDAR</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../linux/linuxdrivelist.cpp" line="119"/>
        <source>Internal SD card reader</source>
        <translation>Lector de tarjetas SD interno</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="77"/>
        <source>Use OS customization?</source>
        <translation>¿Usar la personalización del SO?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="92"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>¿Desea aplicar los ajustes de personalización del SO?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="134"/>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NO, BORRAR AJUSTES</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="125"/>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="102"/>
        <source>EDIT SETTINGS</source>
        <translation>EDITAR AJUSTES</translation>
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
        <location filename="../main.qml" line="119"/>
        <location filename="../main.qml" line="481"/>
        <source>Raspberry Pi Device</source>
        <translation>Dispositivo Raspberry Pi</translation>
    </message>
    <message>
        <location filename="../main.qml" line="131"/>
        <source>CHOOSE DEVICE</source>
        <translation>ELEGIR DISPOSITIVO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="143"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Seleccione este botón para elegir su Raspberry Pi objetivo</translation>
    </message>
    <message>
        <location filename="../main.qml" line="157"/>
        <location filename="../main.qml" line="584"/>
        <source>Operating System</source>
        <translation>Sistema operativo</translation>
    </message>
    <message>
        <location filename="../main.qml" line="168"/>
        <location filename="../main.qml" line="1638"/>
        <source>CHOOSE OS</source>
        <translation>ELEGIR SO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>Select this button to change the operating system</source>
        <translation>Seleccione este botón para cambiar el sistema operativo</translation>
    </message>
    <message>
        <location filename="../main.qml" line="194"/>
        <location filename="../main.qml" line="979"/>
        <source>Storage</source>
        <translation>Almacenamiento</translation>
    </message>
    <message>
        <location filename="../main.qml" line="330"/>
        <source>Network not ready yet</source>
        <translation>La red aún no está lista</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1007"/>
        <source>No storage devices found</source>
        <translation>No se han encontrado dispositivos de almacenamiento</translation>
    </message>
    <message>
        <location filename="../main.qml" line="205"/>
        <location filename="../main.qml" line="1317"/>
        <source>CHOOSE STORAGE</source>
        <translation>ELEGIR ALMACENAMIENTO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="219"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Seleccione este botón para cambiar el dispositivo de almacenamiento de destino</translation>
    </message>
    <message>
        <location filename="../main.qml" line="265"/>
        <source>CANCEL WRITE</source>
        <translation>CANCELAR ESCRITURA</translation>
    </message>
    <message>
        <location filename="../main.qml" line="268"/>
        <location filename="../main.qml" line="1240"/>
        <source>Cancelling...</source>
        <translation>Cancelando...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="280"/>
        <source>CANCEL VERIFY</source>
        <translation>CANCELAR VERIFICACIÓN</translation>
    </message>
    <message>
        <location filename="../main.qml" line="283"/>
        <location filename="../main.qml" line="1263"/>
        <location filename="../main.qml" line="1336"/>
        <source>Finalizing...</source>
        <translation>Finalizando...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="292"/>
        <source>Next</source>
        <translation>Siguiente</translation>
    </message>
    <message>
        <location filename="../main.qml" line="298"/>
        <source>Select this button to start writing the image</source>
        <translation>Seleccione este botón para empezar a escribir la imagen</translation>
    </message>
    <message>
        <location filename="../main.qml" line="320"/>
        <source>Using custom repository: %1</source>
        <translation>Usando repositorio personalizado: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="339"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navegación por teclado: &lt;tab&gt; navegar al botón siguiente &lt;space&gt; pulsar botón/seleccionar elemento &lt;arrow up/down&gt; subir/bajar en listas</translation>
    </message>
    <message>
        <location filename="../main.qml" line="360"/>
        <source>Language: </source>
        <translation>Idioma: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="383"/>
        <source>Keyboard: </source>
        <translation>Teclado: </translation>
    </message>
    <message>
        <location filename="../main.qml" line="500"/>
        <source>[ All ]</source>
        <translation>[ Todos ]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Back</source>
        <translation>Volver</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <source>Go back to main menu</source>
        <translation>Volver al menú principal</translation>
    </message>
    <message>
        <location filename="../main.qml" line="894"/>
        <source>Released: %1</source>
        <translation>Publicado: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="904"/>
        <source>Cached on your computer</source>
        <translation>En caché en su ordenador</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Local file</source>
        <translation>Archivo local</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>Online - %1 GB download</source>
        <translation>En línea - descarga de %1 GB</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1042"/>
        <location filename="../main.qml" line="1094"/>
        <location filename="../main.qml" line="1100"/>
        <source>Mounted as %1</source>
        <translation>Montado como %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1096"/>
        <source>[WRITE PROTECTED]</source>
        <translation>[PROTEGIDO CONTRA ESCRITURA]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1141"/>
        <source>Are you sure you want to quit?</source>
        <translation>¿Está seguro de que quiere salir?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1142"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager sigue ocupado.&lt;br&gt;¿Está seguro de que quiere salir?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1153"/>
        <source>Warning</source>
        <translation>Advertencia</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1162"/>
        <source>Preparing to write...</source>
        <translation>Preparando para escribir...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1176"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Se borrarán todos los datos existentes en &apos;%1&apos;.&lt;br&gt;¿Está seguro de que desea continuar?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1191"/>
        <source>Update available</source>
        <translation>Actualización disponible</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1192"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Hay una versión más reciente de Imager disponible.&lt;br&gt;¿Desea visitar el sitio web para descargarla?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1243"/>
        <source>Writing... %1%</source>
        <translation>Escribiendo... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1266"/>
        <source>Verifying... %1%</source>
        <translation>Verificando... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1273"/>
        <source>Preparing to write... (%1)</source>
        <translation>Preparando para escribir... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1293"/>
        <source>Error</source>
        <translation>Error</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1300"/>
        <source>Write Successful</source>
        <translation>Escritura exitosa</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1301"/>
        <location filename="../imagewriter.cpp" line="596"/>
        <source>Erase</source>
        <translation>Borrar</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1302"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; se ha borrado&lt;br&gt;&lt;br&gt;Ya puede retirar la tarjeta SD del lector</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1309"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; se ha escrito en &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Ya puede retirar la tarjeta SD del lector</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1463"/>
        <source>Error parsing os_list.json</source>
        <translation>Error al parsear os_list.json</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="597"/>
        <source>Format card as FAT32</source>
        <translation>Formatear tarjeta como FAT32</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="603"/>
        <source>Use custom</source>
        <translation>Usar personalizado</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="604"/>
        <source>Select a custom .img from your computer</source>
        <translation>Seleccione un .img personalizado de su ordenador</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1712"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Conecte primero una memoria USB que contenga imágenes.&lt;br&gt;Las imágenes deben estar ubicadas en la carpeta raíz de la memoria USB.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1728"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>La tarjeta SD está protegida contra escritura.&lt;br&gt;Pulse hacia arriba el interruptor de bloqueo situado en el lado izquierdo de la tarjeta y vuelva a intentarlo.</translation>
    </message>
</context>
</TS>
