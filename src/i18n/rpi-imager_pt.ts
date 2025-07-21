<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="pt_PT">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <source>Error extracting archive: %1</source>
        <translation>Erro ao extrair o arquivo: %1</translation>
    </message>
    <message>
        <source>Error mounting FAT32 partition</source>
        <translation>Erro ao montar a partição FAT32</translation>
    </message>
    <message>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>O sistema operativo não montou a partição FAT32</translation>
    </message>
    <message>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Erro ao mudar para o diretório &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation type="unfinished">Erro na leitura do armazenamento.&lt;br&gt;O cartão SD pode estar danificado.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation type="unfinished">Erro ao verificar a gravação. O conteúdo do cartão SD é diferente do que foi gravado nele.</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <source>unmounting drive</source>
        <translation>desmontando unidade</translation>
    </message>
    <message>
        <source>opening drive</source>
        <translation>abrindo unidade</translation>
    </message>
    <message>
        <source>Error running diskpart: %1</source>
        <translation>Erro ao executar o diskpart: %1</translation>
    </message>
    <message>
        <source>Error removing existing partitions</source>
        <translation>Erro ao remover partições existentes</translation>
    </message>
    <message>
        <source>Authentication cancelled</source>
        <translation>Autenticação cancelada</translation>
    </message>
    <message>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Erro ao executar o authopen para obter acesso ao dispositivo de disco &apos;%1&apos;</translation>
    </message>
    <message>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Verifique se o “Raspberry Pi Imager” tem permissão de acesso a &apos;volumes amovíveis&apos; nas definições de privacidade (em &apos;ficheiros e pastas&apos; ou, em alternativa, dê-lhe &apos;acesso total ao disco&apos;).</translation>
    </message>
    <message>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Não foi possível abrir o dispositivo de armazenamento &apos;%1&apos;.</translation>
    </message>
    <message>
        <source>discarding existing data on drive</source>
        <translation>descartar dados existentes na unidade</translation>
    </message>
    <message>
        <source>zeroing out first and last MB of drive</source>
        <translation>zerar o primeiro e o último MB da unidade</translation>
    </message>
    <message>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Erro de gravação ao zerar o MBR</translation>
    </message>
    <message>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Erro de gravação ao tentar zerar a última parte do cartão.&lt;br&gt;O cartão pode estar a anunciar uma capacidade errada (possível contrafação).</translation>
    </message>
    <message>
        <source>starting download</source>
        <translation>iniciar a transferência</translation>
    </message>
    <message>
        <source>Error downloading: %1</source>
        <translation>Erro ao transferir: %1</translation>
    </message>
    <message>
        <source>Access denied error while writing file to disk.</source>
        <translation>Erro de acesso negado durante a gravação do ficheiro no disco.</translation>
    </message>
    <message>
        <source>Error writing file to disk</source>
        <translation>Erro ao gravar o ficheiro no disco</translation>
    </message>
    <message>
        <source>Error writing to storage (while flushing)</source>
        <translation>Erro ao gravar na memória (durante a limpeza)</translation>
    </message>
    <message>
        <source>Error writing to storage (while fsync)</source>
        <translation>Erro ao gravar no armazenamento (durante fsync)</translation>
    </message>
    <message>
        <source>Error writing first block (partition table)</source>
        <translation>Erro ao gravar o primeiro bloco (tabela de partições)</translation>
    </message>
    <message>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Erro na leitura do armazenamento.&lt;br&gt;O cartão SD pode estar danificado.</translation>
    </message>
    <message>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Erro ao verificar a gravação. O conteúdo do cartão SD é diferente do que foi gravado nele.</translation>
    </message>
    <message>
        <source>Customizing image</source>
        <translation>A personalizar imagem</translation>
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
        <translation>Erro ao formatar (via udisks2)</translation>
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
        <translation type="unfinished">Armazenamento</translation>
    </message>
    <message>
        <source>No storage devices found</source>
        <translation type="unfinished">Não foram encontrados dispositivos de armazenamento</translation>
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
        <translation type="unfinished">[PROTEGIDO CONTRA GRAVAÇÃO]</translation>
    </message>
    <message>
        <source>SYSTEM</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation type="unfinished">O cartão SD está protegido contra a gravação.&lt;br&gt;Empurre o interrutor de bloqueio no lado esquerdo do cartão para cima e tente novamente.</translation>
    </message>
</context>
<context>
    <name>HWListModel</name>
    <message>
        <source>CHOOSE DEVICE</source>
        <translation type="unfinished">ESCOLHER DISPOSITIVO</translation>
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
        <translation>A capacidade de armazenamento não é suficientemente grande.&lt;br&gt;Precisa de ter pelo menos %1 GB.</translation>
    </message>
    <message>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>O ficheiro de entrada não é uma imagem de disco válida.&lt;br&gt;O tamanho do ficheiro %1 bytes não é um múltiplo de 512 bytes.</translation>
    </message>
    <message>
        <source>Downloading and writing image</source>
        <translation>A transferir e a gravar imagem</translation>
    </message>
    <message>
        <source>Select image</source>
        <translation>Selecionar imagem</translation>
    </message>
    <message>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Erro ao sincronizar a hora. Tentar novamente daqui a 3 segundos</translation>
    </message>
    <message>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>O STP está ativado no seu comutador de  Ethernet. A obtenção de IP demorará muito tempo.</translation>
    </message>
    <message>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Gostaria de pré-preencher a palavra-passe wifi a partir do chaveiro do sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <source>opening image file</source>
        <translation>abrindo ficheiro de imagem</translation>
    </message>
    <message>
        <source>Error opening image file</source>
        <translation>Erro ao abrir ficheiro de imagem</translation>
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
        <translation>NÃO</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>SIM</translation>
    </message>
    <message>
        <source>CONTINUE</source>
        <translation>CONTINUAR</translation>
    </message>
    <message>
        <source>QUIT</source>
        <translation>SAIR</translation>
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
        <translation type="unfinished">Voltar</translation>
    </message>
    <message>
        <source>Go back to main menu</source>
        <translation type="unfinished">Voltar ao menu principal</translation>
    </message>
    <message>
        <source>Released: %1</source>
        <translation type="unfinished">Lançado: %1</translation>
    </message>
    <message>
        <source>Cached on your computer</source>
        <translation type="unfinished">Em cache no seu computador</translation>
    </message>
    <message>
        <source>Local file</source>
        <translation type="unfinished">Ficheiro local</translation>
    </message>
    <message>
        <source>Online - %1 GB download</source>
        <translation type="unfinished">Online - transferir %1 GB</translation>
    </message>
    <message>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation type="unfinished">Insira primeiro uma pen USB que contenha imagens.&lt;br&gt;As imagens devem estar localizadas na pasta de raiz da pen USB.</translation>
    </message>
</context>
<context>
    <name>OptionsGeneralTab</name>
    <message>
        <source>Set hostname:</source>
        <translation type="unfinished">Definir o nome de anfitrião:</translation>
    </message>
    <message>
        <source>Set username and password</source>
        <translation type="unfinished">Definir nome de utilizador e palavra-passe</translation>
    </message>
    <message>
        <source>Username:</source>
        <translation type="unfinished">Nome de utilizador:</translation>
    </message>
    <message>
        <source>Password:</source>
        <translation type="unfinished">Palavra-passe:</translation>
    </message>
    <message>
        <source>Configure wireless LAN</source>
        <translation type="unfinished">Configurar a rede LAN sem fios</translation>
    </message>
    <message>
        <source>SSID:</source>
        <translation type="unfinished">SSID:</translation>
    </message>
    <message>
        <source>Hidden SSID</source>
        <translation type="unfinished">SSID oculto</translation>
    </message>
    <message>
        <source>Wireless LAN country:</source>
        <translation type="unfinished">País da rede LAN sem fios:</translation>
    </message>
    <message>
        <source>Set locale settings</source>
        <translation type="unfinished">Definir definições de idioma e região</translation>
    </message>
    <message>
        <source>Time zone:</source>
        <translation type="unfinished">Fuso horário:</translation>
    </message>
    <message>
        <source>Keyboard layout:</source>
        <translation type="unfinished">Disposição do teclado:</translation>
    </message>
</context>
<context>
    <name>OptionsMiscTab</name>
    <message>
        <source>Play sound when finished</source>
        <translation type="unfinished">Reproduzir som ao terminar</translation>
    </message>
    <message>
        <source>Eject media when finished</source>
        <translation type="unfinished">Ejetar suporte ao terminar</translation>
    </message>
    <message>
        <source>Enable telemetry</source>
        <translation type="unfinished">Ativar telemetria</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <source>OS Customization</source>
        <translation>Personalização do SO</translation>
    </message>
    <message>
        <source>General</source>
        <translation>Geral</translation>
    </message>
    <message>
        <source>Services</source>
        <translation>Serviços</translation>
    </message>
    <message>
        <source>Options</source>
        <translation>Opções</translation>
    </message>
    <message>
        <source>SAVE</source>
        <translation>GUARDAR</translation>
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
        <translation type="unfinished">Ativar SSH</translation>
    </message>
    <message>
        <source>Use password authentication</source>
        <translation type="unfinished">Utilizar autenticação por palavra-passe</translation>
    </message>
    <message>
        <source>Allow public-key authentication only</source>
        <translation type="unfinished">Permitir apenas a autenticação por chave pública</translation>
    </message>
    <message>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation type="unfinished">Definir authorized_keys para &apos;%1&apos;:</translation>
    </message>
    <message>
        <source>Delete Key</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>RUN SSH-KEYGEN</source>
        <translation type="unfinished">EXECUTAR SSH-KEYGEN</translation>
    </message>
    <message>
        <source>Add SSH Key</source>
        <translation type="unfinished"></translation>
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
        <translation>Gostaria de aplicar definições de personalização do SO?</translation>
    </message>
    <message>
        <source>NO</source>
        <translation>NÃO</translation>
    </message>
    <message>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NÃO, LIMPAR DEFINIÇÕES</translation>
    </message>
    <message>
        <source>YES</source>
        <translation>SIM</translation>
    </message>
    <message>
        <source>EDIT SETTINGS</source>
        <translation>EDITAR DEFINIÇÕES</translation>
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
        <translation>Selecione este botão para escolher o seu Raspberry Pi de destino</translation>
    </message>
    <message>
        <source>Operating System</source>
        <translation>Sistema operativo</translation>
    </message>
    <message>
        <source>CHOOSE OS</source>
        <translation>ESCOLHER SO</translation>
    </message>
    <message>
        <source>Select this button to change the operating system</source>
        <translation>Selecione este botão para mudar o sistema operativo</translation>
    </message>
    <message>
        <source>Storage</source>
        <translation>Armazenamento</translation>
    </message>
    <message>
        <source>Network not ready yet</source>
        <translation>A rede ainda não está pronta</translation>
    </message>
    <message>
        <source>CHOOSE STORAGE</source>
        <translation>ESCOLHER ARMAZENAMENTO</translation>
    </message>
    <message>
        <source>Select this button to change the destination storage device</source>
        <translation>Selecione este botão para mudar o dispositivo de armazenamento de destino</translation>
    </message>
    <message>
        <source>CANCEL WRITE</source>
        <translation>CANCELAR A GRAVAÇÃO</translation>
    </message>
    <message>
        <source>Cancelling...</source>
        <translation>A cancelar...</translation>
    </message>
    <message>
        <source>CANCEL VERIFY</source>
        <translation>CANCELAR VERIFICAÇÃO</translation>
    </message>
    <message>
        <source>Finalizing...</source>
        <translation>A finalizar...</translation>
    </message>
    <message>
        <source>Next</source>
        <translation>Seguinte</translation>
    </message>
    <message>
        <source>Select this button to start writing the image</source>
        <translation>Selecione este botão para começar a gravar a imagem</translation>
    </message>
    <message>
        <source>Using custom repository: %1</source>
        <translation>A usar repositório personalizado: %1</translation>
    </message>
    <message>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navegação pelo teclado: &lt;tab&gt; navegar para o botão seguinte &lt;space&gt; premir o botão/selecionar o item &lt;seta para cima/para baixo&gt; subir/descer nas listas</translation>
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
        <translation>Tem a certeza de que pretende sair?</translation>
    </message>
    <message>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>O Raspberry Pi Imager ainda está ocupado.&lt;br&gt;Tem a certeza de que pretende sair?</translation>
    </message>
    <message>
        <source>Warning</source>
        <translation>Aviso</translation>
    </message>
    <message>
        <source>Preparing to write...</source>
        <translation>A preparar para gravar...</translation>
    </message>
    <message>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Todos os dados existentes em &apos;%1 serão apagados.&lt;br&gt;Tem a certeza de que pretende continuar?</translation>
    </message>
    <message>
        <source>Update available</source>
        <translation>Atualização disponível</translation>
    </message>
    <message>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Está disponível uma versão mais recente do Imager.&lt;br&gt;Deseja visitar o website para a transferir?</translation>
    </message>
    <message>
        <source>Writing... %1%</source>
        <translation>A gravar... %1%</translation>
    </message>
    <message>
        <source>Verifying... %1%</source>
        <translation>A verificar... %1%</translation>
    </message>
    <message>
        <source>Preparing to write... (%1)</source>
        <translation>A preparar para gravar... (%1)</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>Erro</translation>
    </message>
    <message>
        <source>Write Successful</source>
        <translation>Gravado com sucesso</translation>
    </message>
    <message>
        <source>Erase</source>
        <translation>Apagar</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; foi apagado &lt;br&gt;&lt;br&gt;Pode agora remover o cartão SD do leitor</translation>
    </message>
    <message>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; foi gravado para &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Pode agora remover o cartão SD do leitor</translation>
    </message>
    <message>
        <source>Format card as FAT32</source>
        <translation>Formatar cartão como FAT32</translation>
    </message>
    <message>
        <source>Use custom</source>
        <translation>Utilizar personalizado</translation>
    </message>
    <message>
        <source>Select a custom .img from your computer</source>
        <translation>Selecione um .img personalizado do seu computador</translation>
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
