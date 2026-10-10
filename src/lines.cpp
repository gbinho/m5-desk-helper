#include "lines.h"

#include "config.h"

namespace lines {

#define PICK(list) list[random(sizeof(list) / sizeof(list[0]))]

const Timed TIMED[] = {
    {12, 30, "Já almoçou, " OWNER "?"},
    {18, 0, "Boa noite, " OWNER "! Como foi o dia?"},
    {22, 30, "Tá ficando tarde, " OWNER "..."},
};
const size_t TIMED_COUNT = sizeof(TIMED) / sizeof(TIMED[0]);

const char* greeting(int hour) {
  if (hour < 5) return "Ainda acordado, " OWNER "?";
  if (hour < 12) return "Bom dia, " OWNER "!";
  if (hour < 18) return "Boa tarde, " OWNER "!";
  return "Boa noite, " OWNER "!";
}

static const Joke JOKES[] = {
    {"O que o pato disse para a pata?", "Vem quá!"},
    {"O que é um pontinho verde no canto da sala?", "Uma ervilha de castigo!"},
    {"O que é um pontinho amarelo no céu?", "Um yellowcóptero!"},
    {"Por que a plantinha não foi atendida no hospital?", "Porque só tinha médico de plantão!"},
    {"O que o zero disse para o oito?", "Que cinto maneiro!"},
    {"Qual é o doce preferido do átomo?", "Pé de moléculas!"},
    {"O que a impressora falou para a outra?", "Essa folha é sua ou é impressão minha?"},
    {"O que o tomate foi fazer no banco?", "Tirar extrato!"},
    {"Qual é o contrário de volátil?", "Vem cá, sobrinho!"},
    {"Por que o jacaré tirou o filho da escola?", "Porque ele réptil de ano!"},
    {"O que o canibal vegetariano come?", "A planta do pé!"},
    {"Como o Batman faz para abrir a bat-caverna?", "Ele bat-palma!"},
    {"Qual é a fórmula da água benta?", "H Deus O!"},
    {"Por que o programador confunde Halloween com Natal?", "Porque OCT 31 = DEC 25!"},
    {"Qual é o café mais perigoso do mundo?", "O ex-presso!"},
    {"Qual é o rei dos queijos?", "O reiqueijão!"},
    {"O que são dois pontos pretos no microscópio?", "Uma blacktéria e um pretozoário!"},
    {"Por que o pinheiro não se perde na floresta?", "Porque ele tem um mapinha!"},
    {"Qual é o cereal preferido do vampiro?", "Aveia!"},
    {"Como se chama o peixe que caiu do décimo andar?", "Aaaaah... tum!"},
    {"O que o cavalo foi fazer no orelhão?", "Passar um trote!"},
    {"Sabe por que a vaca foi para o espaço?", "Para se encontrar com o vácuo!"},
    {"Por que o computador foi ao médico?", "Porque estava com vírus!"},
    {"O que a galinha foi fazer na igreja?", "Assistir à missa do galo!"},
};

static const char* const NAME[] = {
    "Tô de olho em você, " OWNER "!",
    "Bora, " OWNER "! Você consegue!",
    OWNER ", você é fera!",
    "Ei, " OWNER "... só passando pra dar oi!",
    OWNER ", já salvou o arquivo?",
    "Postura, " OWNER "! Coluna reta!",
    "Tá focado, hein, " OWNER "?",
    OWNER ", me faz um carinho? Aperta o botão duas vezes!",
};
static const char* const PETTED[] = {"Valeu, " OWNER "!", "Hehe, faz de novo!", OWNER " é o melhor!",
                                     "Adoro carinho!"};
static const char* const SHAKEN[] = {"Para, " OWNER "!!", "Tô tonto...", "Ei! Devagar aí!"};
static const char* const STARTLED[] = {"Que susto, " OWNER "!", "Eita!", "Quem tá aí?!"};
static const char* const WOKEN[] = {"Hmm? Oi, " OWNER "...", "Já é de manhã?", "Só mais cinco minutinhos..."};
static const char* const WATER[] = {OWNER ", hora de beber água!", "Bebe uma aguinha, " OWNER "!"};
static const char* const STRETCH[] = {"Levanta e alonga, " OWNER "!", "Hora de esticar as pernas, " OWNER "!"};

const Joke& joke() { return PICK(JOKES); }
const char* name() { return PICK(NAME); }
const char* petted() { return PICK(PETTED); }
const char* shaken() { return PICK(SHAKEN); }
const char* startled() { return PICK(STARTLED); }
const char* woken() { return PICK(WOKEN); }
const char* water() { return PICK(WATER); }
const char* stretch() { return PICK(STRETCH); }

}  // namespace lines
