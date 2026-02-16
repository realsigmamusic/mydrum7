# MyDrum7

![Platform](https://img.shields.io/badge/platform-Linux-linux)
![Format](https://img.shields.io/badge/format-LV2-orange)
![License](https://img.shields.io/badge/license-GPLv3-blue)

**MyDrum7** é um plugin de bateria acústica virtual de alta fidelidade no formato **LV2**. Projetado para produtores e músicos que buscam uma sonoridade *orgânica e realista* em suas produções no Linux.

Este instrumento virtual combina a performance da linguagem C++ com uma biblioteca de sons meticulosamente gravada, oferecendo dinâmica e expressividade comparáveis a softwares comerciais de ponta.

<p align="center">
  <img src="./docs/screenshot.png" alt="MyDrum7 screenshot">
  <br>
  <img src="./docs/mixer.png" alt="Mixer View">
</p>

## Funcionalidades

* **Dinâmica Realista:** Múltiplas camadas de velocidade (*Velocity Layers*) para cada peça da bateria. O timbre muda naturalmente dependendo da força com que a nota é tocada.
* **Round Robin (RR):** Sistema inteligente que alterna entre diferentes amostras para a mesma nota e intensidade, eliminando o efeito robótico ("metralhadora") em repetições rápidas.
* **Saídas Múltiplas (Multi-Output):** O plugin oferece **12 canais de áudio independentes**, permitindo que você mixe o Bumbo, Caixa, Tons e Overheads em faixas separadas na sua DAW, exatamente como faria com uma gravação de bateria real.
* **Motor de Hi-Hat Avançado:** Inclui grupos de "Choke" para cortar o som do chimbal aberto quando o pedal é pressionado, garantindo uma performance autêntica.

<p align="center">
  <img src="./docs/build-routing-confirmation.png" alt="build routing confirmation">
  <br>
  <img src="./docs/output.png" alt="Outputs View">
</p>

## Instalação

1.  Baixe a versão mais recente na aba [**Releases**](https://github.com/realsigmamusic/mydrum7/releases).
2.  Extraia o arquivo compactado.
3.  Mova a pasta para o diretório de plugins do seu usuário:
    ```bash
    mkdir -p ~/.lv2
    mv mydrum7.lv2 ~/.lv2/
    ```
4.  Abra sua DAW favorita (Ardour, Reaper, Qtractor, etc.) e busque por **MyDrum7**.

> **Nota:** Para instalar para todos os usuários do sistema, mova a pasta para `/usr/lib/lv2/` (requer privilégios de root).

---

## Roteamento de Áudio (Canais)

Para obter a melhor mixagem, roteie as saídas do plugin para trilhas de áudio mono/estéreo na sua DAW. O plugin expõe 12 portas de saída:

| Porta | Peça / Microfone | Descrição |
| :---: | :--- | :--- |
| **1** | **Kick In** | Microfone interno do Bumbo (Ataque/Click) |
| **2** | **Kick Out** | Microfone externo do Bumbo (Peso/Sub) |
| **3** | **Snare Top** | Microfone superior da Caixa |
| **4** | **Snare Bottom** | Microfone da esteira da Caixa |
| **5** | **Hi-Hat** | Microfone direto do Chimbal |
| **6** | **Rack Tom 1** | Tom Agudo |
| **7** | **Rack Tom 2** | Tom Médio |
| **8** | **Rack Tom 3** | Tom Grave |
| **9** | **Floor Tom 1** | Surdo 1 |
| **10** | **Floor Tom 2** | Surdo 2 |
| **11-12** | **Overheads** | Microfones de ambiente (Par Estéreo L/R) |

## Mapa MIDI

O MyDrum7 segue um mapeamento intuitivo, compatível com a norma GM (General MIDI) em muitas peças, facilitando o uso com baterias eletrônicas e controladores.

## Créditos e Tecnologia

Este projeto foi construído sobre ombros de gigantes, utilizando tecnologias open-source de ponta para áudio.

* **Samples:** A alma deste plugin são as amostras de áudio de alta qualidade fornecidas gentilmente por [**Tchackpoum**](https://www.tchackpoum.com/). Visite o site para conhecer mais sobre o trabalho deles.
* **Tecnologias:**
    * **LV2:** O padrão aberto para plugins de áudio no Linux.
    * **C++:** Utilizado para máxima performance e baixa latência no processamento de sinal (DSP).
    * **libsndfile:** Para leitura e decodificação de áudio de alta fidelidade.