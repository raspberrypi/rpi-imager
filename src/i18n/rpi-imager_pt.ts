<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="pt_PT">
<context>
    <name>DownloadExtractThread</name>
    <message>
        <location filename="../downloadextractthread.cpp" line="196"/>
        <location filename="../downloadextractthread.cpp" line="385"/>
        <source>Error extracting archive: %1</source>
        <translation>Erro ao extrair o arquivo: %1</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="261"/>
        <source>Error mounting FAT32 partition</source>
        <translation>Erro ao montar a partição FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="281"/>
        <source>Operating system did not mount FAT32 partition</source>
        <translation>O sistema operativo não montou a partição FAT32</translation>
    </message>
    <message>
        <location filename="../downloadextractthread.cpp" line="304"/>
        <source>Error changing to directory &apos;%1&apos;</source>
        <translation>Erro ao mudar para o diretório &apos;%1&apos;</translation>
    </message>
</context>
<context>
    <name>DownloadThread</name>
    <message>
        <location filename="../downloadthread.cpp" line="118"/>
        <source>unmounting drive</source>
        <translation>desmontando unidade</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="138"/>
        <source>opening drive</source>
        <translation>abrindo unidade</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="166"/>
        <source>Error running diskpart: %1</source>
        <translation>Erro ao executar o diskpart: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="187"/>
        <source>Error removing existing partitions</source>
        <translation>Erro ao remover partições existentes</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="213"/>
        <source>Authentication cancelled</source>
        <translation>Autenticação cancelada</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="216"/>
        <source>Error running authopen to gain access to disk device &apos;%1&apos;</source>
        <translation>Erro ao executar o authopen para obter acesso ao dispositivo de disco &apos;%1&apos;</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="217"/>
        <source>Please verify if &apos;Raspberry Pi Imager&apos; is allowed access to &apos;removable volumes&apos; in privacy settings (under &apos;files and folders&apos; or alternatively give it &apos;full disk access&apos;).</source>
        <translation>Verifique se o “Raspberry Pi Imager” tem permissão de acesso a &apos;volumes amovíveis&apos; nas definições de privacidade (em &apos;ficheiros e pastas&apos; ou, em alternativa, dê-lhe &apos;acesso total ao disco&apos;).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="239"/>
        <source>Cannot open storage device &apos;%1&apos;.</source>
        <translation>Não foi possível abrir o dispositivo de armazenamento &apos;%1&apos;.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="281"/>
        <source>discarding existing data on drive</source>
        <translation>descartar dados existentes na unidade</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="301"/>
        <source>zeroing out first and last MB of drive</source>
        <translation>zerar o primeiro e o último MB da unidade</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="307"/>
        <source>Write error while zero&apos;ing out MBR</source>
        <translation>Erro de gravação ao zerar o MBR</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="319"/>
        <source>Write error while trying to zero out last part of card.&lt;br&gt;Card could be advertising wrong capacity (possible counterfeit).</source>
        <translation>Erro de gravação ao tentar zerar a última parte do cartão.&lt;br&gt;O cartão pode estar a anunciar uma capacidade errada (possível contrafação).</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="408"/>
        <source>starting download</source>
        <translation>iniciar a transferência</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="468"/>
        <source>Error downloading: %1</source>
        <translation>Erro ao transferir: %1</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="665"/>
        <source>Access denied error while writing file to disk.</source>
        <translation>Erro de acesso negado durante a gravação do ficheiro no disco.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="670"/>
        <source>Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.</source>
        <translation>O Acesso Controlado a Pastas parece estar ativado. Adicione o rpi-imager.exe e o fat32format.exe à lista de aplicações permitidas e tente novamente.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="677"/>
        <source>Error writing file to disk</source>
        <translation>Erro ao gravar o ficheiro no disco</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="699"/>
        <source>Download corrupt. Hash does not match</source>
        <translation>Transferência corrupta. O hash não coincide</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="711"/>
        <location filename="../downloadthread.cpp" line="763"/>
        <source>Error writing to storage (while flushing)</source>
        <translation>Erro ao gravar na memória (durante a limpeza)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="718"/>
        <location filename="../downloadthread.cpp" line="770"/>
        <source>Error writing to storage (while fsync)</source>
        <translation>Erro ao gravar no armazenamento (durante fsync)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="753"/>
        <source>Error writing first block (partition table)</source>
        <translation>Erro ao gravar o primeiro bloco (tabela de partições)</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="828"/>
        <source>Error reading from storage.&lt;br&gt;SD card may be broken.</source>
        <translation>Erro na leitura do armazenamento.&lt;br&gt;O cartão SD pode estar danificado.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="847"/>
        <source>Verifying write failed. Contents of SD card is different from what was written to it.</source>
        <translation>Erro ao verificar a gravação. O conteúdo do cartão SD é diferente do que foi gravado nele.</translation>
    </message>
    <message>
        <location filename="../downloadthread.cpp" line="900"/>
        <source>Customizing image</source>
        <translation>A personalizar imagem</translation>
    </message>
</context>
<context>
    <name>DriveFormatThread</name>
    <message>
        <location filename="../driveformatthread.cpp" line="63"/>
        <location filename="../driveformatthread.cpp" line="124"/>
        <location filename="../driveformatthread.cpp" line="185"/>
        <source>Error partitioning: %1</source>
        <translation>Erro de particionamento: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="84"/>
        <source>Error starting fat32format</source>
        <translation>Erro ao iniciar o fat32format</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="94"/>
        <source>Error running fat32format: %1</source>
        <translation>Erro ao executar o fat32format: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="104"/>
        <source>Error determining new drive letter</source>
        <translation>Erro ao determinar a nova letra de unidade</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="109"/>
        <source>Invalid device: %1</source>
        <translation>Dispositivo inválido: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="146"/>
        <source>Error formatting (through udisks2)</source>
        <translation>Erro ao formatar (via udisks2)</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="174"/>
        <source>Error starting sfdisk</source>
        <translation>Erro ao iniciar o sfdisk</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="199"/>
        <source>Partitioning did not create expected FAT partition %1</source>
        <translation>O particionamento não criou a partição FAT pretendida %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="208"/>
        <source>Error starting mkfs.fat</source>
        <translation>Erro ao iniciar o mkfs.fat</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="218"/>
        <source>Error running mkfs.fat: %1</source>
        <translation>Erro ao executar o mkfs.fat: %1</translation>
    </message>
    <message>
        <location filename="../driveformatthread.cpp" line="225"/>
        <source>Formatting not implemented for this platform</source>
        <translation>Formatação não implementada para esta plataforma</translation>
    </message>
</context>
<context>
    <name>ImageWriter</name>
    <message>
        <location filename="../imagewriter.cpp" line="253"/>
        <source>Storage capacity is not large enough.&lt;br&gt;Needs to be at least %1 GB.</source>
        <translation>A capacidade de armazenamento não é suficientemente grande.&lt;br&gt;Precisa de ter pelo menos %1 GB.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="259"/>
        <source>Input file is not a valid disk image.&lt;br&gt;File size %1 bytes is not a multiple of 512 bytes.</source>
        <translation>O ficheiro de entrada não é uma imagem de disco válida.&lt;br&gt;O tamanho do ficheiro %1 bytes não é um múltiplo de 512 bytes.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="654"/>
        <source>Downloading and writing image</source>
        <translation>A transferir e a gravar imagem</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="787"/>
        <source>Select image</source>
        <translation>Selecionar imagem</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="962"/>
        <source>Error synchronizing time. Trying again in 3 seconds</source>
        <translation>Erro ao sincronizar a hora. Tentar novamente daqui a 3 segundos</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="974"/>
        <source>STP is enabled on your Ethernet switch. Getting IP will take long time.</source>
        <translation>O STP está ativado no seu comutador de  Ethernet. A obtenção de IP demorará muito tempo.</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="1185"/>
        <source>Would you like to prefill the wifi password from the system keychain?</source>
        <translation>Gostaria de pré-preencher a palavra-passe wifi a partir do chaveiro do sistema?</translation>
    </message>
</context>
<context>
    <name>LocalFileExtractThread</name>
    <message>
        <location filename="../localfileextractthread.cpp" line="34"/>
        <source>opening image file</source>
        <translation>abrindo ficheiro de imagem</translation>
    </message>
    <message>
        <location filename="../localfileextractthread.cpp" line="39"/>
        <source>Error opening image file</source>
        <translation>Erro ao abrir ficheiro de imagem</translation>
    </message>
</context>
<context>
    <name>MsgPopup</name>
    <message>
        <location filename="../MsgPopup.qml" line="98"/>
        <source>NO</source>
        <translation>NÃO</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="107"/>
        <source>YES</source>
        <translation>SIM</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="116"/>
        <source>CONTINUE</source>
        <translation>CONTINUAR</translation>
    </message>
    <message>
        <location filename="../MsgPopup.qml" line="124"/>
        <source>QUIT</source>
        <translation>SAIR</translation>
    </message>
</context>
<context>
    <name>OptionsPopup</name>
    <message>
        <location filename="../OptionsPopup.qml" line="20"/>
        <source>OS Customization</source>
        <translation>Personalização do SO</translation>
    </message>
    <message>
        <source>OS customization options</source>
        <translation>Opções de personalização do SO</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="62"/>
        <source>General</source>
        <translation>Geral</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="70"/>
        <source>Services</source>
        <translation>Serviços</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="73"/>
        <source>Options</source>
        <translation>Opções</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="89"/>
        <source>Set hostname:</source>
        <translation>Definir o nome de anfitrião:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="112"/>
        <source>Set username and password</source>
        <translation>Definir nome de utilizador e palavra-passe</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="134"/>
        <source>Username:</source>
        <translation>Nome de utilizador:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="151"/>
        <location filename="../OptionsPopup.qml" line="220"/>
        <source>Password:</source>
        <translation>Palavra-passe:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="187"/>
        <source>Configure wireless LAN</source>
        <translation>Configurar a rede LAN sem fios</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="206"/>
        <source>SSID:</source>
        <translation>SSID:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="239"/>
        <source>Show password</source>
        <translation>Mostrar palavra-passe</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="245"/>
        <source>Hidden SSID</source>
        <translation>SSID oculto</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="251"/>
        <source>Wireless LAN country:</source>
        <translation>País da rede LAN sem fios:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="262"/>
        <source>Set locale settings</source>
        <translation>Definir definições de idioma e região</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="272"/>
        <source>Time zone:</source>
        <translation>Fuso horário:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="282"/>
        <source>Keyboard layout:</source>
        <translation>Disposição do teclado:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="299"/>
        <source>Enable SSH</source>
        <translation>Ativar SSH</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="318"/>
        <source>Use password authentication</source>
        <translation>Utilizar autenticação por palavra-passe</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="328"/>
        <source>Allow public-key authentication only</source>
        <translation>Permitir apenas a autenticação por chave pública</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="346"/>
        <source>Set authorized_keys for &apos;%1&apos;:</source>
        <translation>Definir authorized_keys para &apos;%1&apos;:</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="358"/>
        <source>RUN SSH-KEYGEN</source>
        <translation>EXECUTAR SSH-KEYGEN</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="376"/>
        <source>Play sound when finished</source>
        <translation>Reproduzir som ao terminar</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="380"/>
        <source>Eject media when finished</source>
        <translation>Ejetar suporte ao terminar</translation>
    </message>
    <message>
        <location filename="../OptionsPopup.qml" line="384"/>
        <source>Enable telemetry</source>
        <translation>Ativar telemetria</translation>
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
        <translation>Leitor de cartões SD interno</translation>
    </message>
</context>
<context>
    <name>UseSavedSettingsPopup</name>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="77"/>
        <source>Use OS customization?</source>
        <translation>Utilizar a personalização do SO?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="92"/>
        <source>Would you like to apply OS customization settings?</source>
        <translation>Gostaria de aplicar definições de personalização do SO?</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="134"/>
        <source>NO</source>
        <translation>NÃO</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="115"/>
        <source>NO, CLEAR SETTINGS</source>
        <translation>NÃO, LIMPAR DEFINIÇÕES</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="125"/>
        <source>YES</source>
        <translation>SIM</translation>
    </message>
    <message>
        <location filename="../UseSavedSettingsPopup.qml" line="102"/>
        <source>EDIT SETTINGS</source>
        <translation>EDITAR DEFINIÇÕES</translation>
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
        <translation>ESCOLHER DISPOSITIVO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="143"/>
        <source>Select this button to choose your target Raspberry Pi</source>
        <translation>Selecione este botão para escolher o seu Raspberry Pi de destino</translation>
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
        <translation>ESCOLHER SO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="180"/>
        <source>Select this button to change the operating system</source>
        <translation>Selecione este botão para mudar o sistema operativo</translation>
    </message>
    <message>
        <location filename="../main.qml" line="194"/>
        <location filename="../main.qml" line="979"/>
        <source>Storage</source>
        <translation>Armazenamento</translation>
    </message>
    <message>
        <location filename="../main.qml" line="330"/>
        <source>Network not ready yet</source>
        <translation>A rede ainda não está pronta</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1007"/>
        <source>No storage devices found</source>
        <translation>Não foram encontrados dispositivos de armazenamento</translation>
    </message>
    <message>
        <location filename="../main.qml" line="205"/>
        <location filename="../main.qml" line="1317"/>
        <source>CHOOSE STORAGE</source>
        <translation>ESCOLHER ARMAZENAMENTO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="219"/>
        <source>Select this button to change the destination storage device</source>
        <translation>Selecione este botão para mudar o dispositivo de armazenamento de destino</translation>
    </message>
    <message>
        <location filename="../main.qml" line="265"/>
        <source>CANCEL WRITE</source>
        <translation>CANCELAR A GRAVAÇÃO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="268"/>
        <location filename="../main.qml" line="1240"/>
        <source>Cancelling...</source>
        <translation>A cancelar...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="280"/>
        <source>CANCEL VERIFY</source>
        <translation>CANCELAR VERIFICAÇÃO</translation>
    </message>
    <message>
        <location filename="../main.qml" line="283"/>
        <location filename="../main.qml" line="1263"/>
        <location filename="../main.qml" line="1336"/>
        <source>Finalizing...</source>
        <translation>A finalizar...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="292"/>
        <source>Next</source>
        <translation>Seguinte</translation>
    </message>
    <message>
        <location filename="../main.qml" line="298"/>
        <source>Select this button to start writing the image</source>
        <translation>Selecione este botão para começar a gravar a imagem</translation>
    </message>
    <message>
        <location filename="../main.qml" line="320"/>
        <source>Using custom repository: %1</source>
        <translation>A usar repositório personalizado: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="339"/>
        <source>Keyboard navigation: &lt;tab&gt; navigate to next button &lt;space&gt; press button/select item &lt;arrow up/down&gt; go up/down in lists</source>
        <translation>Navegação pelo teclado: &lt;tab&gt; navegar para o botão seguinte &lt;space&gt; premir o botão/selecionar o item &lt;seta para cima/para baixo&gt; subir/descer nas listas</translation>
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
        <translation>[ Tudo ]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="651"/>
        <source>Back</source>
        <translation>Voltar</translation>
    </message>
    <message>
        <location filename="../main.qml" line="652"/>
        <source>Go back to main menu</source>
        <translation>Voltar ao menu principal</translation>
    </message>
    <message>
        <location filename="../main.qml" line="894"/>
        <source>Released: %1</source>
        <translation>Lançado: %1</translation>
    </message>
    <message>
        <location filename="../main.qml" line="904"/>
        <source>Cached on your computer</source>
        <translation>Em cache no seu computador</translation>
    </message>
    <message>
        <location filename="../main.qml" line="906"/>
        <source>Local file</source>
        <translation>Ficheiro local</translation>
    </message>
    <message>
        <location filename="../main.qml" line="907"/>
        <source>Online - %1 GB download</source>
        <translation>Online - transferir %1 GB</translation>
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
        <translation>[PROTEGIDO CONTRA GRAVAÇÃO]</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1141"/>
        <source>Are you sure you want to quit?</source>
        <translation>Tem a certeza de que pretende sair?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1142"/>
        <source>Raspberry Pi Imager is still busy.&lt;br&gt;Are you sure you want to quit?</source>
        <translation>O Raspberry Pi Imager ainda está ocupado.&lt;br&gt;Tem a certeza de que pretende sair?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1153"/>
        <source>Warning</source>
        <translation>Aviso</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1162"/>
        <source>Preparing to write...</source>
        <translation>A preparar para gravar...</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1176"/>
        <source>All existing data on &apos;%1&apos; will be erased.&lt;br&gt;Are you sure you want to continue?</source>
        <translation>Todos os dados existentes em &apos;%1 serão apagados.&lt;br&gt;Tem a certeza de que pretende continuar?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1191"/>
        <source>Update available</source>
        <translation>Atualização disponível</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1192"/>
        <source>There is a newer version of Imager available.&lt;br&gt;Would you like to visit the website to download it?</source>
        <translation>Está disponível uma versão mais recente do Imager.&lt;br&gt;Deseja visitar o website para a transferir?</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1243"/>
        <source>Writing... %1%</source>
        <translation>A gravar... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1266"/>
        <source>Verifying... %1%</source>
        <translation>A verificar... %1%</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1273"/>
        <source>Preparing to write... (%1)</source>
        <translation>A preparar para gravar... (%1)</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1293"/>
        <source>Error</source>
        <translation>Erro</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1300"/>
        <source>Write Successful</source>
        <translation>Gravado com sucesso</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1301"/>
        <location filename="../imagewriter.cpp" line="596"/>
        <source>Erase</source>
        <translation>Apagar</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1302"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been erased&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; foi apagado &lt;br&gt;&lt;br&gt;Pode agora remover o cartão SD do leitor</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1309"/>
        <source>&lt;b&gt;%1&lt;/b&gt; has been written to &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;You can now remove the SD card from the reader</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; foi gravado para &lt;b&gt;%2&lt;/b&gt;&lt;br&gt;&lt;br&gt;Pode agora remover o cartão SD do leitor</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1463"/>
        <source>Error parsing os_list.json</source>
        <translation>Erro ao analisar os_list.json</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="597"/>
        <source>Format card as FAT32</source>
        <translation>Formatar cartão como FAT32</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="603"/>
        <source>Use custom</source>
        <translation>Utilizar personalizado</translation>
    </message>
    <message>
        <location filename="../imagewriter.cpp" line="604"/>
        <source>Select a custom .img from your computer</source>
        <translation>Selecione um .img personalizado do seu computador</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1712"/>
        <source>Connect an USB stick containing images first.&lt;br&gt;The images must be located in the root folder of the USB stick.</source>
        <translation>Insira primeiro uma pen USB que contenha imagens.&lt;br&gt;As imagens devem estar localizadas na pasta de raiz da pen USB.</translation>
    </message>
    <message>
        <location filename="../main.qml" line="1728"/>
        <source>SD card is write protected.&lt;br&gt;Push the lock switch on the left side of the card upwards, and try again.</source>
        <translation>O cartão SD está protegido contra a gravação.&lt;br&gt;Empurre o interrutor de bloqueio no lado esquerdo do cartão para cima e tente novamente.</translation>
    </message>
</context>
</TS>
