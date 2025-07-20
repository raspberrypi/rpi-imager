<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="it_IT">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Errore estrazione archivio: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Errore montaggio partizione FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>Il sistema operativo non ha montato la partizione FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Errore passaggio a cartella &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Errore lettura dallo storage.&lt;br&gt;La scheda SD potrebbe essere danneggiata.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verifica scrittura fallita.&lt;br&gt;Il contenuto della SD è differente da quello che vi è stato scritto.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>smontaggio unità</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>apertura unità</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Errore esecuzione diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Errore rimozione partizioni esistenti</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Autenticazione annullata</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Errore esecuzione auhopen per ottenere accesso al dispositivo disco %1</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Verifica se a &apos;Raspberry Pi Imager&apos; è consentito l&apos;accesso a &apos;volumi rimovibili&apos; nelle impostazioni privacy (in &apos;file e cartelle&apos; o in alternativa concedi &apos;accesso completo al disco&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Impossibile aprire dispositivo storage &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>elimina i dati esistenti nell&apos;unità</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>azzera il primo e l&apos;ultimo MB dell&apos;unità</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Errore scrittura durante azzeramento MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Errore di scrittura durante il tentativo di azzerare l&apos;ultima parte della scheda.&lt;br&gt;La scheda potrebbe riportare una capacità maggiore di quella reale (possibile contraffazione).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>avvio download</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Errore download: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Errore accesso negato durante la scrittura del file su disco.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Errore scrittura file su disco</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Errore scrittura nello storage (durante flushing)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Errore scrittura nello storage (durante fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Errore scrittura primo blocco (tabella partizione)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Errore lettura dallo storage.&lt;br&gt;La scheda SD potrebbe essere danneggiata.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Verifica scrittura fallita.&lt;br&gt;Il contenuto della SD è differente da quello che vi è stato scritto.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>Personalizza immagine</translation>
    </message>
    <message>
        <source>Cached file is corrupt. SHA256 hash does not match expected value.&lt;br&gt;The cache file will be removed and the download will restart.</source>
        <translation>Il file memorizzata nella cache è corrotto.&lt;br&gt;L&apos;hash SHA256 non corrisponde al valore previsto.&lt;br&gt; Il file cache verrà rimosso ed il download si riavvierà.</translation>
    </message>
    <message>
        <source>Local file is corrupt or has incorrect SHA256 hash.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2</source>
        <translation>Il file locale è corrotto o ha l&apos;hash SHA256 errato. &lt;br&gt;Previsto: %1 &lt;br&gt;Effettivo: %2</translation>
    </message>
    <message>
        <source>Download appears to be corrupt. SHA256 hash does not match.&lt;br&gt;Expected: %1&lt;br&gt;Actual: %2&lt;br&gt;Please check your network connection and try again.</source>
        <translation>Il download sembra essere corrotto.&lt;br&gt;L&apos;hash SHA256 non corrisponde.&lt;br&gt;Previsto: %1&lt;br&gt;Effettivo: %2&lt;br&gt;Controlla la connessione di rete e riprova.</translation>
    </message>
    <message>
        <source>Controlled Folder Access seems to be enabled. Please add rpi-imager.exe to the list of allowed apps and try again.</source>
        <translation>L&apos;accesso controllato alla cartella sembra sia abilitato. Aggiungi rpi-imager.exe all&apos;elenco delle app consentite e riprova.</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <source>Error partitioning: %1</source>
        <translation>Errore partizionamento: %1</translation>
    </message>
    <message>
        <source>Error formatting (through udisks2)</source>
        <translation>Errore formattazione (attraverso udisk2)</translation>
    </message>
    <message>
        <source>Formatting not implemented for this platform</source>
        <translation>Formattazione non implementata per questa piattaforma</translation>
    </message>
    <message>
        <source>Error opening device for formatting</source>
        <translation>Errore apertura dispositivo durante la formattazione</translation>
    </message>
    <message>
        <source>Error writing to device during formatting</source>
        <translation>Errore scrittura dispositivo durante la formattazione</translation>
    </message>
    <message>
        <source>Error seeking on device during formatting</source>
        <translation>Errore ricerca dispositivo durante la formattazione</translation>
    </message>
    <message>
        <source>Invalid parameters for formatting</source>
        <translation>Parametro non valido per la formattazione</translation>
    </message>
    <message>
        <source>Insufficient space on device</source>
        <translation>Spazio insufficiente nel dispositivo</translation>
    </message>
    <message>
        <source>Unknown formatting error</source>
        <translation>Errore formattazione sconosciuto</translation>
    </message>
</context>
<context>
    <name>DstPopup</name>
    <message>
        <source>Storage</source>
        <translation>Storage</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation>Nessun dispositivo archiviazione trovato</translation>
    </message>
    <message>
        <source>Exclude System Drives</source>
        <translation>Escludi unità sistema</translation>
    </message>
    <message>
        <source>gigabytes</source>
        <translation>Gigabyte</translation>
    </message>
    <message>
        <source>Mounted as %1</source>
        <translation>Montato come %1</translation>
    </message>
    <message>
        <source>GB</source>
        <translation>GB</translation>
    </message>
    <message>
        <source>[WRITE PROTECTED]</source>
        <translation>[PROTETTA DA SCRITTURA]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation>SISTEMA</translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>La scheda SD è protetta da scrittura.&lt;br&gt;Sposta verso l&apos;alto l&apos;interruttore LOCK sul lato sinistro della scheda SD e riprova.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation>SCEGLI DISPOSITIVO</translation>
    </message>
</context>
<context>
    <name>HwPopup</name>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Dispositivo Raspberry Pi</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>La capacità dello storage non è sufficiente.&lt;br&gt;Sono necessari almeno %1 GB.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>Il file sorgente non è un&apos;immagine disco valida.&lt;br&gt;La dimensione file %1 non è un multiplo di 512 byte.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>Download e scrittura file immagine</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Seleziona file immagine</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Errore durante la sincronizzazione dell&apos;ora, riprova tra 3 secondi</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>STP è abilitato sullo switch Ethernet. Ottenere l&apos;IP richiederà molto tempo.</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Vuoi precompilare la password WiFi usando il portachiavi di sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>apertura file immagine</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Errore durante l&apos;apertura del file immagine</translation>
    </message>
    <message>
        <source>starting extraction</source>
        <translation>avvio estrazione</translation>
    </message>
    <message>
        <source>Error reading from image file</source>
        <translation>Errore lettura file immagine</translation>
    </message>
    <message>
        <source>Error writing to device</source>
        <translation>Errore scrittura file immagine</translation>
    </message>
    <message>
        <source>Failed to read complete image file</source>
        <translation>Impossibile leggere il file immagine completo</translation>
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
        <translation>SI</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>CONTINUA</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>ESCI</translation>
    </message>
</context>
<context>
    <name>OSListModel</name>
    <message>
        <source>Recommended</source>
        <translation>Raccomandato</translation>
    </message>
</context>
<context>
    <name>OSPopup</name>
    <message>
        <source>Operating System</source>
        <translation>Sistema operativo</translation>
    </message>
    <message>
        <source>Back</source>
        <translation>Indietro</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation>Torna al menu principale</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation>Rilasciato: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation>Memorizzato temporaneamente nel computer</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation>File locale</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation>Online - Download %1 GB</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Prima collega una chiavetta USB contenente il file immagine.&lt;br&gt;Il file immagine deve essere presente nella cartella principale della chiavetta USB.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation>Imposta nome host:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation>Imposta nome utente e password</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation>Nome utente:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation>Password:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation>Configura WiFi</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation>SSID nascosto</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation>Nazione WiFi:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation>Imposta configurazione locale</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation>Fuso orario:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation>Layout tastiera:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation>Riproduci suono quando completato</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation>Espelli media quando completato</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation>Abilita telemetria</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Personalizzazione SO</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Generale</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Servizi</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Opzioni</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>SALVA</translation>
    </message>
    <message>
        <source>CANCEL</source>
        <translation>ANNULLA</translation>
    </message>
    <message>
        <source>Please fix validation errors in General and Services tabs</source>
        <translation>Correggi gli errori di convalida nelle schede Generale e Servizi</translation>
    </message>
    <message>
        <source>Please fix validation errors in General tab</source>
        <translation>Correggi gli errori di convalida nella scheda Generale</translation>
    </message>
    <message>
        <source>Please fix validation errors in Services tab</source>
        <translation>Correggi gli errori di convalida nella scheda Servizi</translation>
    </message>
</context>
<context>
    <name>OptionsServicesTab</name>
    <message>
        <source>Enable SSH</source>
        <translation>Abilita SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation>Usa password autenticazione</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation>Consenti solo autenticazione con chiave pubblica</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>Imposta authorized_keys per &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation>Chiave predefinita</translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation>ESEGUI SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation>Aggiungi chiave SSH</translation>
    </message>
    <message>
        <source>Paste your SSH public key here.
Supported formats: ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, and SSH certificates
Example: ssh-rsa AAAAB3NzaC1yc2E... user@hostname</source>
        <translation>Incolla qui la chiave pubblica SSH.
Formati supportati: ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sa2-nistp256@openssh.com e certificati SSH
Esempio: ssh-rsa aaab3nzac1yc2e ... user@hostname</translation>
    </message>
    <message>
        <source>Invalid SSH key format. SSH keys must start with ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com, or SSH certificates, followed by the key data and optional comment.</source>
        <translation>Formato chiave SSH non valido. Le chiavi SSH devono iniziare con ssh-rsa, ssh-ed25519, ssh-dss, ssh-ecdsa, sk-ssh-ed25519@openssh.com, sk-ecdsa-sha2-nistp256@openssh.com o certificati SSH, seguiti dai dati chiave e dal commento opzionale.</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Vuoi applicare le impostazioni personalizzazione sistema operativo?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NO</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NO, AZZERA IMPOSTAZIONI</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>SI&apos;</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>MODIFICA IMPOSTAZIONI</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>Raspberry Pi Imager v%1</source>
        <translation>Raspberry Pi Imager v. %1</translation>
    </message>
    <message>
        <source>Raspberry Pi Device</source>
        <translation>Dispositivo Raspberry Pi</translation>
    </message>
    <message>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Seleziona questo pulsante per scegliere il Raspberry Pi destinazione</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Sistema operativo</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>SCEGLI S.O.</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Seleziona questo pulsante per modificare il sistema operativo scelto</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Scheda SD</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>Rete non ancora pronta</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>SCEGLI SCHEDA SD</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Seleziona questo pulsante per modificare il dispositivo archiviazione destinazione</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>ANNULLA SCRITTURA</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>Annullamento...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>ANNULLA VERIFICA</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>Finalizzazione...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Avanti</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Seleziona questo pulsante per avviare la scrittura del file immagine</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>Usa repository personalizzato: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navigazione da tastiera: &lt;tab&gt; vai al prossimo pulsante &lt;spazio&gt; premi il pulsante/seleziona la voce &lt;freccia su/giù&gt; vai su/giù negli elenchi</translation>
    </message>
    <message>
        <source>Language: </source>
        <translation>Lingua: </translation>
    </message>
    <message>
        <source>Keyboard: </source>
        <translation>Tastiera: </translation>
    </message>
    <message>
        <source>Are you sure you want to quit?</source>
        <translation>Sei sicuro di voler uscire?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>Raspberry Pi Image è occupato.&lt;br&gt;Sei sicuro di voler uscire?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Attenzione</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>Preparazione scrittura...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Tutti i dati esistenti in &apos;%1&apos; verranno eliminati.&lt;br&gt;Sei sicuro di voler continuare?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Aggiornamento disponibile</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>È disponibile una nuova versione di Imager.&lt;br&gt;Vuoi visitare il sito web per scaricare la nuova versione?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>Scrittura...%1</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>Verifica...%1</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>Preparazione scrittura... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Errore</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Scrittura completata senza errori</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Cancella</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Azzeramento di &lt;b&gt;%1&lt;/b&gt; completato&lt;br&gt;&lt;br&gt;Ora puoi rimuovere la scheda SD dal lettore</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>Scrittura di &lt;b&gt;%1&lt;/b&gt; in &lt;b&gt;%2&lt;/b&gt; completata&lt;br&gt;&lt;br&gt;Ora puoi rimuovere la scheda SD dal lettore</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formatta scheda come FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Usa immagine personalizzata</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Seleziona un file immagine .img personalizzato</translation>
    </message>
    <message>
        <source>SKIP CACHE VERIFICATION</source>
        <translation>SALTA VERIFICA CACHE</translation>
    </message>
    <message>
        <source>Starting download...</source>
        <translation>Avvio download...</translation>
    </message>
    <message>
        <source>Verifying cached file... %1%</source>
        <translation>Verifica file nella cache... %1%</translation>
    </message>
    <message>
        <source>Verifying cached file...</source>
        <translation>Verifica file nella cache...</translation>
    </message>
    <message>
        <source>Starting write...</source>
        <translation>Avvio scrittura...</translation>
    </message>
</context>
</TS>
