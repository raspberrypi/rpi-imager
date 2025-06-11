<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="es_ES">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Error extrayendo el archivo: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Error montando la partición FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>El sistema operativo no montó la partición FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Error cambiando al directorio &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Error leyendo del almacenamiento.&lt;br&gt;La tarjeta SD puede estar rota.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Error verificando la escritura. El contenido de la tarjeta SD es diferente del que se escribió en ella.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>desmontando unidad</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>abriendo unidad</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Error ejecutando diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Error eliminando las particiones existentes</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Autenticación cancelada</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Error ejecutando authopen para acceder al dispositivo de disco &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Por favor, compruebe si &apos;Raspberry Pi Imager&apos; tiene permitido el acceso a &apos;volúmenes extraíbles&apos; en los ajustes de privacidad (en &apos;archivos y carpetas&apos; o alternativamente dele &apos;acceso total al disco&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>No se puede abrir el dispositivo de almacenamiento &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>descartando datos existentes en la unidad</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>poniendo a cero el primer y el último MB de la unidad</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Error de escritura al poner a cero MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Error de escritura al intentar poner a cero la última parte de la tarjeta.&lt;br&gt;La tarjeta podría estar anunciando una capacidad incorrecta (posible falsificación).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>iniciando descarga</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Error descargando: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Error de acceso denegado escribiendo el archivo en el disco.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>El acceso controlado a carpetas parece estar activado. Añada rpi-imager.exe y fat32format.exe a la lista de aplicaciones permitidas y vuelva a intentarlo.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Error escribiendo el archivo en el disco</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Error escribiendo en la memoria (durante la limpieza)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Error escribiendo en el almacenamiento (mientras fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Error escribiendo el primer bloque (tabla de particiones)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Error leyendo del almacenamiento.&lt;br&gt;La tarjeta SD puede estar rota.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Error verificando la escritura. El contenido de la tarjeta SD es diferente del que se escribió en ella.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Personalizando imagen</translation>
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
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error partitioning: %1</source>
        <translation>Error particionando: %1</translation>
    </message>
    <message>
        <source>Error starting fat32format</source>
        <translation>Error iniciando fat32format</translation>
    </message>
    <message>
        <source>Error running fat32format: %1</source>
        <translation>Error ejecutando fat32format: %1</translation>
    </message>
    <message>
        <source>Error determining new drive letter</source>
        <translation>Error determinando la nueva letra de unidad</translation>
    </message>
    <message>
        <source>Invalid device: %1</source>
        <translation>Dispositivo no válido: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Error formateando (a través de udisks2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Formateo no implementado para esta plataforma</translation>
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
        <translation type="unfinished">Almacenamiento</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished">No se han encontrado dispositivos de almacenamiento</translation>
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
        <translation type="unfinished">Montado como %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation type="unfinished">[PROTEGIDO CONTRA ESCRITURA]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">La tarjeta SD está protegida contra escritura.&lt;br&gt;Pulse hacia arriba el interruptor de bloqueo situado en el lado izquierdo de la tarjeta y vuelva a intentarlo.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">ELEGIR DISPOSITIVO</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation type="unfinished">Dispositivo Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>La capacidad de almacenamiento no es lo suficientemente grande.&lt;br&gt;Necesita ser de al menos %1 GB.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>El archivo de entrada no es una imagen de disco válida.&lt;br&gt;El tamaño del archivo %1 bytes no es múltiplo de 512 bytes.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Descargando y escribiendo imagen</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Seleccionar imagen</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Error sincronizando la hora. Vuelva a intentarlo en 3 segundos</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>El comutador de Ethernet tiene el STP activado. La obtención de la IP puede tardar mucho.</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>¿Desea rellenar previamente la contraseña wifi desde el llavero del sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>abriendo archivo de imagen</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Error abriendo archivo de imagen</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation type="unfinished"></translation>
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
        <translation>CONTINUAR</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>SALIR</translation>
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
        <translation type="unfinished">Sistema operativo</translation>
    </message>
    <message>
        <source>Back</source>
        <translation type="unfinished">Volver</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Volver al menú principal</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Publicado: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">En caché en su ordenador</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Archivo local</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">En línea - descarga de %1 GB</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Conecte primero una memoria USB que contenga imágenes.&lt;br&gt;Las imágenes deben estar ubicadas en la carpeta raíz de la memoria USB.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Establecer nombre de anfitrión:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Establecer nombre de usuario y contraseña</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Nombre de usuario:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Contraseña:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Configurar LAN inalámbrica</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">SSID oculta</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">País de LAN inalámbrica:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Establecer ajustes regionales</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Zona horaria:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Distribución del teclado:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Reproducir sonido al finalizar</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Expulsar soporte al finalizar</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Activar telemetría</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Personalización del SO</translation>
    </message>
    <message>
        <source>General</source>
        <translation>General</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Servicios</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Opciones</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>GUARDAR</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation type="unfinished">Activar SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Usar autenticación por contraseña</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Permitir solo la autenticación de clave pública</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Establecer authorized_keys para &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">EJECUTAR SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>¿Desea aplicar los ajustes de personalización del SO?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NO, BORRAR AJUSTES</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>SÍ</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>EDITAR AJUSTES</translation>
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
        <translation>Dispositivo Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Seleccione este botón para elegir su Raspberry Pi objetivo</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Sistema operativo</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>ELEGIR SO</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Seleccione este botón para cambiar el sistema operativo</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Almacenamiento</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>La red aún no está lista</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>ELEGIR ALMACENAMIENTO</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Seleccione este botón para cambiar el dispositivo de almacenamiento de destino</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>CANCELAR ESCRITURA</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Cancelando...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>CANCELAR VERIFICACIÓN</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Finalizando...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Siguiente</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Seleccione este botón para empezar a escribir la imagen</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Usando repositorio personalizado: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navegación por teclado: &lt;tab&gt; navegar al botón siguiente &lt;space&gt; pulsar botón/seleccionar elemento &lt;arrow up/down&gt; subir/bajar en listas</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Idioma: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Teclado: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>¿Está seguro de que quiere salir?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Imager sigue ocupado.&lt;br&gt;¿Está seguro de que quiere salir?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Advertencia</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Preparando para escribir...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Se borrarán todos los datos existentes en &apos;%1&apos;.&lt;br&gt;¿Está seguro de que desea continuar?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Actualización disponible</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Hay una versión más reciente de Imager disponible.&lt;br&gt;¿Desea visitar el sitio web para descargarla?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Escribiendo... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Verificando... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Preparando para escribir... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Error</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Escritura exitosa</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Borrar</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; se ha borrado&lt;br&gt;&lt;br&gt;Ya puede retirar la tarjeta SD del lector</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; se ha escrito en &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Ya puede retirar la tarjeta SD del lector</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formatear tarjeta como FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Usar personalizado</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Seleccione un .img personalizado de su ordenador</translation>
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
