# SA-MP Mobilador para Android

Cliente Android de SA-MP para GTA: San Andreas, com suporte a teclado e mouse. O objetivo é aproximar a experiência de jogar no PC em um dispositivo Android, mantendo também os controles por toque.

> **Status:** em desenvolvimento. A compatibilidade e o desempenho dependem do dispositivo, da versão do Android e do servidor.

## Recursos

- Cliente Android para dispositivos **ARM64**.
- Suporte a teclado e mouse, com teclas, botões e sensibilidades configuráveis.
- Movimento do mouse para controlar a câmera; por padrão, o botão esquerdo dispara e o direito mira.
- Menu de configuração acessível pelo comando `/mob` no chat.
- Controles touch preservados.
- Suporte a páginas de memória de 16 KB.

O projeto busca oferecer uma experiência próxima à do PC, mas nem todos os recursos ou comportamentos são necessariamente idênticos. Não há garantia de uma taxa de quadros específica: FPS varia conforme o aparelho, a temperatura, a tela e a situação do jogo.

## Requisitos

- Android 9 ou superior.
- Dispositivo ARM64.
- Uma instalação legítima de GTA: San Andreas compatível com o cliente. Os arquivos do jogo não são distribuídos neste repositório.

## Instalação

Instale o APK gerado e siga as instruções do launcher para configurar os dados do jogo. Use somente arquivos de jogo obtidos por meios oficiais e não redistribua esses arquivos.

## Compilação

Configure o Android SDK e o NDK 26.2.11394342, depois execute na raiz do repositório:

```bash
bash ./gradlew :app:assembleDebug
```

O APK de debug é gerado em:

```text
app/build/outputs/apk/debug/GTA-2.11-debug.apk
```

Esse APK é destinado a testes e desenvolvimento; uma publicação exige uma build de release e assinatura apropriada.

## Créditos e marcas

Este projeto deriva de trabalho anterior de [Egor Kuzn](https://github.com/bkuzn2) e [Vadim](https://github.com/kuzia15). Consulte os avisos de autoria e licença presentes no código-fonte.

Grand Theft Auto, San Andreas e SA-MP são marcas de seus respectivos proprietários. Este projeto não é afiliado nem endossado por eles.
