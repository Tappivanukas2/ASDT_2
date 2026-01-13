/* Kääntäminen g++ -pthread -O2 -w lipulla tällä hetkellä */

/* Harjoituksen 2 ohjelmarunko */
/* Tee runkoon tarvittavat muutokset kommentoiden */
/* Pistesuorituksista kooste tähän alkuun */
/* 2p = täysin tehty suoritus, 1p = osittain tehty suoritus */
/* Kaikki ainakin yritetty tehdä 2p*/
/* Tarkemmat ohjeet Moodlessa */
/* Lisäohjeita, vinkkejä ja apuja löytyy koodin joukosta */
/* OPISKELIJA: merkityt kohdat eritoten kannattaa katsoa huolella */

//peruskirjastot mitä tarvii aika lailla aina kehitystyössä
//OPISKELIJA: lisää tarvitsemasi peruskirjastot

#include <iostream>
#include <clocale>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>   // ADDED: RLE file input
#include <cctype>    // ADDED: std::isdigit, std::isspace

//rinnakaisuuden peruskirjastot
//OPISKLEIJA: lisää tarvittaessa lisää kirjastoja, muista käyttää -pthread lippua käännöksessä ja tarvittaessa -lrt lippua myös
//Huom! Rinnakkaisuusasioista on eri versioita (c++, POSIX/pthread, SystemV)
//Kaikkien käyttö on sallittua
#include <sys/ipc.h>
#include <sys/shm.h> // shmget(), shmat(), shmdt(), shmctl()
#include <unistd.h>
#include <pthread.h> //pthread perussäiemäärittelyt
#include <cstdlib>

using namespace std;

//definet voi jättää globaalille alueelle, ne on sitten tiedossa koko tiedostossa
#define KORKEUS 100 //rivien määrä alla
#define LEVEYS 100 //sarakkaiden määrä alla
//#define KORKEUS 7 //rivien määrä alla - pieni labyrintti
//#define LEVEYS 7 //sarakkaiden määrä alla - pieni labyrintti
#define ROTTIEN_LKMC 4 //kuinka monta rottaa

//Pointteri labyrinttia varten - viittaa jaettuun muistiin
int (*labyrintti)[LEVEYS] = nullptr;

//Rottien sijaintien seuranta
struct RattaSijainti {
    int ykoord;
    int xkoord;
    bool valmis;
};
RattaSijainti rottienSijainnit[ROTTIEN_LKMC];

// -------------------------
// RLE-based labyrinth loading
// -------------------------
//
// Specification match:
// - global 2D labyrinth array removed; labyrinth lives in shared memory (labyrintti pointer)
// - parent/main initializes shared memory and writes labyrinth cells there
// - works for any labyrinth of size KORKEUS x LEVEYS (RLE must decode to exactly that)
//
// RLE format used here (UNAMBIGUOUS):
//   "<count>:<value>" pairs separated by whitespace (spaces/newlines).
// Example:
//   "12:1 3:0 5:1"
// means 12 cells of value 1, then 3 cells of value 0, then 5 cells of value 1.
static bool loadRLEFile(const std::string& path, std::string& outRle) {
    std::ifstream in(path);
    if (!in) return false;

    std::string all;
    std::string line;
    bool lastWasSpace = true;

    while (std::getline(in, line)) {
        // Strip // comments
        const auto pos = line.find("//");
        if (pos != std::string::npos) line = line.substr(0, pos);

        // Normalize whitespace to single spaces (keeps delimiters between pairs)
        for (unsigned char uc : line) {
            if (std::isspace(uc)) {
                if (!lastWasSpace) {
                    all.push_back(' ');
                    lastWasSpace = true;
                }
            } else {
                all.push_back(static_cast<char>(uc));
                lastWasSpace = false;
            }
        }

        // Ensure line boundary acts as a separator
        if (!lastWasSpace) {
            all.push_back(' ');
            lastWasSpace = true;
        }
    }

    // Trim trailing spaces
    while (!all.empty() && all.back() == ' ') all.pop_back();

    if (all.empty()) return false;
    outRle = std::move(all);
    return true;
}

static bool decodeDelimitedRLEToSharedLabyrinth(const std::string& rle, int (*dst)[LEVEYS]) {
    const size_t total = static_cast<size_t>(KORKEUS) * static_cast<size_t>(LEVEYS);
    size_t written = 0;

    size_t i = 0;
    while (i < rle.size() && written < total) {
        while (i < rle.size() && std::isspace(static_cast<unsigned char>(rle[i]))) ++i;
        if (i >= rle.size()) break;

        // Parse count
        if (!std::isdigit(static_cast<unsigned char>(rle[i]))) return false;
        size_t count = 0;
        while (i < rle.size() && std::isdigit(static_cast<unsigned char>(rle[i]))) {
            count = count * 10 + static_cast<size_t>(rle[i] - '0');
            ++i;
        }

        // Expect ':'
        if (i >= rle.size() || rle[i] != ':') return false;
        ++i;

        // Parse value (allow multi-digit just in case)
        if (i >= rle.size() || !std::isdigit(static_cast<unsigned char>(rle[i]))) return false;
        int value = 0;
        while (i < rle.size() && std::isdigit(static_cast<unsigned char>(rle[i]))) {
            value = value * 10 + (rle[i] - '0');
            ++i;
        }

        // Write run
        for (size_t k = 0; k < count; ++k) {
            if (written >= total) return false;
            const size_t y = written / static_cast<size_t>(LEVEYS);
            const size_t x = written % static_cast<size_t>(LEVEYS);
            dst[y][x] = value;
            ++written;
        }
    }

    // Allow trailing whitespace
    while (i < rle.size() && std::isspace(static_cast<unsigned char>(rle[i]))) ++i;
    return (written == total) && (i == rle.size());
}

//karttasijainnin tallettamiseen käytettävä rakenne, luotaessa alustuu vasempaan alakulmaan
//HUOM! ykoordinaatti on peilikuva taulukon rivi-indeksiin
//PasiM: TODO, voisi yksinkertaistaa että ykoord olisi sama kuin rivi-indeksi
struct Sijainti {
    int ykoord {0};
    int xkoord {0};
};

//rotan liikkeen suunnan määrittelyyn käytettävä rakenne
//huom! suunnat ovat absoluuttisia kuin kompassissa
//UP -> ykoord++ (yindex--)
//DOWN -> ykoord-- (yindex++)
//LEFT -> xkoord--
//RIGHT -> xkoord++
//DEFAULT -> suunta tuntematon
enum LiikkumisSuunta {
    UP, DOWN, LEFT, RIGHT, DEFAULT
};

//tämän hetken toteutuksessa tämä on tarpeeton rakenne, voisi käyttää rotan omassa kartan ymmärryksen kasvatuksessa
enum Ristausve {
    WALL = 1,
    OPENING = 0
};

//tämä rakenne on jokaisesta risteyksestä jokaiseen suuntaan omansa
//VINKKI: tutkituksi merkittyyn/merkittävään suuntaan ei rotta koskaan lähde enää tutkimaan ;)
struct Suunta {
    Ristausve jatkom; //tutkituille suunnille tämä on määritelty OPENING arvolle
    bool tutkittu {false}; //alkuarvona tutkimaton
};

//TÄRKEIN kaikista rakenteista, kertoo miten risteys on opittu juuri kyseisen rotan taholta
//tutkittavana - arvo kertoo minne suuntaan juuri tämä rotta viimeksi lähtenyt ko risteyksestä tutkimaan
struct Ristaus {
    Sijainti kartalla;
    Suunta up;
    Suunta down;
    Suunta left;
    Suunta right;
    LiikkumisSuunta tutkittavana = DEFAULT; //alkuarvo, kertoo while-silmukan alussa olevalle risteyskoodille että rotta tuli ensimmäistä kertaa ko risteykseen
};

//poikkeuksenheittämistä varten, ei käytössä tällä hetkellä
//PasiM, TODO: poikkeukset
struct Karttavirhe {
    int koodi {0};
    string msg;
};


void* aloitaRotta(void* arg);
void drawMaze();

//etsii kartasta jotain spesifistä, palauttaa sen koordinaatit
Sijainti etsiKartasta(int kohde){
    Sijainti kartalla;
    for (int y = 0; y<KORKEUS ; y++) {
        for (int x = 0; x<LEVEYS ; x++){
            if (labyrintti[y][x] == kohde) {
                kartalla.xkoord = x;
                kartalla.ykoord = KORKEUS-1-y;
                return kartalla;
            }
        }
    }
    return kartalla;
}

//etsitään labyrintin aloituskohta, merkitty 3:lla
Sijainti findBegin(){
    Sijainti alkusijainti;
    alkusijainti = etsiKartasta(3);
    return alkusijainti;
}

//Piirtää labyrintin terminaaliin
void drawMaze() {
    cout << "\n=== LABYRINTTI - LOPPUTILANNE ===\n\n";
    for (int y = 0; y < KORKEUS; y++) {
        for (int x = 0; x < LEVEYS; x++) {
            // Tarkista onko tässä ruudussa rottaa
            bool onRotta = false;
            int rottaCount = 0;
            for (int i = 0; i < ROTTIEN_LKMC; i++) {
                if (rottienSijainnit[i].valmis) {
                    int yindex = KORKEUS - 1 - rottienSijainnit[i].ykoord;
                    if (yindex == y && rottienSijainnit[i].xkoord == x) {
                        onRotta = true;
                        rottaCount++;
                    }
                }
            }

            if (onRotta) {
                // Jos useampi rotta samassa paikassa, näytä määrä
                if (rottaCount > 1) {
                    cout << rottaCount;
                } else {
                    cout << "R";  // rotta
                }
            } else {
                switch (labyrintti[y][x]) {
                    case 0: cout << " "; break;  // tyhjä käytävä
                    case 1: cout << "#"; break;  // seinä
                    case 2: cout << "."; break;  // risteys
                    case 3: cout << "S"; break;  // start
                    case 4: cout << "E"; break;  // exit
                    default: cout << "?"; break;
                }
            }
        }
        cout << "\n";
    }
    cout << u8"\nLegenda: # = seinä, S = alku, E = uloskäynti, . = risteys, R = rotta\n";
    cout << u8"Rotat uloskäynnillä: ";
    int exitCount = 0;
    for (int i = 0; i < ROTTIEN_LKMC; i++) {
        if (rottienSijainnit[i].valmis) {
            int yindex = KORKEUS - 1 - rottienSijainnit[i].ykoord;
            if (labyrintti[yindex][rottienSijainnit[i].xkoord] == 4) {
                exitCount++;
            }
        }
    }
    cout << exitCount << " / " << ROTTIEN_LKMC << "\n";
}

//tutkitaan mitä nykypaikan yläpuolella on, prevDir kertoo minkä suuntainen oli viimeisin kyseisen rotan liikku
bool tutkiUp(Sijainti nykysijainti, std::vector<Ristaus>& reitti, LiikkumisSuunta prevDir){
    int yindex = KORKEUS-1-nykysijainti.ykoord-1;
    if (yindex < 0) return false; //ulos kartalta - ei mahdollista 
    if (labyrintti[yindex][nykysijainti.xkoord] == 1) return false; //labyrintin seinä
    //tulossa uuteen ristaukseen, siihen siirrytään aina silmukan lopuksi nykytoteutuksessa
    if (labyrintti[yindex][nykysijainti.xkoord] == 2 && prevDir != DOWN) {
        Ristaus ristaus;
        ristaus.kartalla.ykoord = nykysijainti.ykoord+1;
        ristaus.kartalla.xkoord = nykysijainti.xkoord;
        ristaus.down.tutkittu = true;
        ristaus.down.jatkom = OPENING;
        reitti.push_back(ristaus); //lisätään risteys pinon päällimmäiseksi
        return true;
    }
    return true;
}
//..alapuolella
bool tutkiDown(Sijainti nykysijainti, std::vector<Ristaus>& reitti, LiikkumisSuunta prevDir){
    int yindex = KORKEUS-1-nykysijainti.ykoord+1;
    if (yindex > KORKEUS-1) return false; //ulos kartalta - ei mahdollista 
    if (labyrintti[yindex][nykysijainti.xkoord] == 1) return false;
    //tulossa uuteen ristaukseen
    if (labyrintti[yindex][nykysijainti.xkoord] == 2 && prevDir != UP){
        Ristaus ristaus;
        ristaus.kartalla.ykoord = nykysijainti.ykoord-1;
        ristaus.kartalla.xkoord = nykysijainti.xkoord;
        ristaus.up.tutkittu = true;
        ristaus.up.jatkom = OPENING;
        reitti.push_back(ristaus);
        return true;
    }
    return true;
}

//..vasemmalla
bool tutkiLeft(Sijainti nykysijainti, std::vector<Ristaus>& reitti, LiikkumisSuunta prevDir){
    int yindex = KORKEUS-1-nykysijainti.ykoord;
    int xindex = nykysijainti.xkoord-1;
    if (xindex < 0) return false; //ulos kartalta - ei mahdollista
    if (labyrintti[yindex][xindex] == 1) return false;
    //tulossa uuteen ristaukseen
    if (labyrintti[yindex][xindex] == 2 && prevDir != RIGHT){
        Ristaus ristaus;
        ristaus.kartalla.ykoord = nykysijainti.ykoord;
        ristaus.kartalla.xkoord = nykysijainti.xkoord-1;
        ristaus.right.tutkittu = true;
        ristaus.right.jatkom = OPENING;
        reitti.push_back(ristaus);
        return true;
    }
    return true;
}

//..oikealla
bool tutkiRight(Sijainti nykysijainti, std::vector<Ristaus>& reitti, LiikkumisSuunta prevDir){
    int yindex = KORKEUS-1-nykysijainti.ykoord;
    int xindex = nykysijainti.xkoord+1;
    if (xindex >= LEVEYS) return false; //ulos kartalta - ei mahdollista
    if (labyrintti[yindex][xindex] == 1) return false;
    //tulossa uuteen ristaukseen
    if (labyrintti[yindex][xindex] == 2 && prevDir != LEFT){
        Ristaus ristaus;
        ristaus.kartalla.ykoord = nykysijainti.ykoord;
        ristaus.kartalla.xkoord = nykysijainti.xkoord+1;
        ristaus.left.tutkittu = true;
        ristaus.left.jatkom = OPENING;
        reitti.push_back(ristaus);
        return true;
    }
    return true;
}

LiikkumisSuunta findNext(bool onkoRistaus, Sijainti nykysijainti, LiikkumisSuunta prevDir, std::vector<Ristaus>& reitti){
    if (!onkoRistaus) {
        if (tutkiLeft(nykysijainti, reitti, prevDir) && prevDir != RIGHT) return LEFT;
        if (tutkiUp(nykysijainti, reitti, prevDir) && prevDir != DOWN) return UP;
        if (tutkiDown(nykysijainti, reitti, prevDir) && prevDir != UP) return DOWN;
        if (tutkiRight(nykysijainti, reitti, prevDir) && prevDir != LEFT) return RIGHT;
        return DEFAULT;
    } else {
        if (tutkiLeft(nykysijainti, reitti, prevDir) && reitti.back().tutkittavana != LEFT && !reitti.back().left.tutkittu) return LEFT;
        if (tutkiUp(nykysijainti, reitti, prevDir) && reitti.back().tutkittavana != UP && !reitti.back().up.tutkittu) return UP;
        if (tutkiDown(nykysijainti, reitti, prevDir) && reitti.back().tutkittavana != DOWN && !reitti.back().down.tutkittu) return DOWN;
        if (tutkiRight(nykysijainti, reitti, prevDir) && reitti.back().tutkittavana != RIGHT && !reitti.back().right.tutkittu) return RIGHT;
        return DEFAULT;
    }
}

Sijainti moveUp(Sijainti nykysijainti){ nykysijainti.ykoord++; return nykysijainti; }
Sijainti moveDown(Sijainti nykysijainti){ nykysijainti.ykoord--; return nykysijainti; }
Sijainti moveLeft(Sijainti nykysijainti){ nykysijainti.xkoord--; return nykysijainti; }
Sijainti moveRight(Sijainti nykysijainti){ nykysijainti.xkoord++; return nykysijainti; }

LiikkumisSuunta doRistaus(Sijainti risteyssijainti, LiikkumisSuunta prevDir, std::vector<Ristaus>& reitti){
    LiikkumisSuunta nextDir;
    nextDir = findNext(true, risteyssijainti, prevDir, reitti);
    if (nextDir == LEFT) reitti.back().tutkittavana = LEFT;
    else if (nextDir == UP) reitti.back().tutkittavana = UP;
    else if (nextDir == RIGHT) reitti.back().tutkittavana = RIGHT;
    else if (nextDir == DOWN) reitti.back().tutkittavana = DOWN;
    else if (nextDir == DEFAULT) reitti.pop_back();
    return nextDir;
}

void* aloitaRotta(void* arg){
    int rottaID = *(int*)arg;
    int liikkuCount=0;
    vector<Ristaus> reitti;
    Sijainti rotanSijainti = findBegin();
    LiikkumisSuunta prevDir {DEFAULT};
    LiikkumisSuunta nextDir {DEFAULT};

    while (labyrintti[KORKEUS-1-rotanSijainti.ykoord][rotanSijainti.xkoord] != 4) {
        if (labyrintti[KORKEUS-1-rotanSijainti.ykoord][rotanSijainti.xkoord] == 2){
            nextDir = doRistaus(rotanSijainti, prevDir, reitti);
        } else {
            nextDir = findNext(false, rotanSijainti, prevDir, reitti);
        }

        switch (nextDir) {
            case UP:    rotanSijainti = moveUp(rotanSijainti);   prevDir = UP; break;
            case DOWN:  rotanSijainti = moveDown(rotanSijainti); prevDir = DOWN; break;
            case LEFT:  rotanSijainti = moveLeft(rotanSijainti); prevDir = LEFT; break;
            case RIGHT: rotanSijainti = moveRight(rotanSijainti);prevDir = RIGHT; break;
            case DEFAULT:
                cout << "Umpikuja: " << "Ruutu: " << rotanSijainti.ykoord << "," << rotanSijainti.xkoord << endl;
                if (reitti.empty()) {
                    rotanSijainti = findBegin();
                    prevDir = DEFAULT;
                    break;
                }
                rotanSijainti.ykoord = reitti.back().kartalla.ykoord;
                rotanSijainti.xkoord = reitti.back().kartalla.xkoord;
                cout << "Palattu: " << "Ruutu: " << rotanSijainti.ykoord << "," << rotanSijainti.xkoord << endl;

                switch (reitti.back().tutkittavana){
                    case UP:    reitti.back().up.tutkittu = true;    reitti.back().up.jatkom = OPENING; break;
                    case DOWN:  reitti.back().down.tutkittu = true;  reitti.back().down.jatkom = OPENING; break;
                    case LEFT:  reitti.back().left.tutkittu = true;  reitti.back().left.jatkom = OPENING; break;
                    case RIGHT: reitti.back().right.tutkittu = true; reitti.back().right.jatkom = OPENING; break;
                    default:
                        cout << "Ei pitäisi tapahtua! Joku ongelma jos tämä tulostus tulee!" << endl;
                        break;
                }
                break;
        }

        liikkuCount++;
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    rottienSijainnit[rottaID].ykoord = rotanSijainti.ykoord;
    rottienSijainnit[rottaID].xkoord = rotanSijainti.xkoord;
    rottienSijainnit[rottaID].valmis = true;

    (void)liikkuCount; // currently unused
    return nullptr;
}

static std::string encodeLabyrinthToDelimitedRLE(const int (*src)[LEVEYS]) {
    const size_t total = static_cast<size_t>(KORKEUS) * static_cast<size_t>(LEVEYS);
    std::string out;
    out.reserve(total / 2);

    auto cellAt = [&](size_t idx) -> int {
        size_t y = idx / static_cast<size_t>(LEVEYS);
        size_t x = idx % static_cast<size_t>(LEVEYS);
        return src[y][x];
    };

    int prev = cellAt(0);
    size_t run = 1;

    for (size_t idx = 1; idx < total; ++idx) {
        int v = cellAt(idx);
        if (v == prev) {
            ++run;
        } else {
            out += std::to_string(run);
            out.push_back(':');
            out += std::to_string(prev);
            out.push_back(' ');
            prev = v;
            run = 1;
        }
    }

    out += std::to_string(run);
    out.push_back(':');
    out += std::to_string(prev);
    out.push_back('\n');

    return out;
}

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rle_file>\n";
        return 1;
    }

    // Create shared memory for labyrinth
    const size_t bytes = sizeof(int) * KORKEUS * LEVEYS;
    const int shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        return 1;
    }

    labyrintti = static_cast<int (*)[LEVEYS]>(shmat(shmid, nullptr, 0));
    if (labyrintti == (void*)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, nullptr);
        labyrintti = nullptr;
        return 1;
    }

    // Load + decode RLE
    std::string rle;
    if (!loadRLEFile(argv[1], rle)) {
        std::cerr << "Failed to read RLE file: " << argv[1] << "\n";
        shmdt(labyrintti);
        shmctl(shmid, IPC_RMID, nullptr);
        labyrintti = nullptr;
        return 1;
    }

    if (!decodeDelimitedRLEToSharedLabyrinth(rle, labyrintti)) {
        std::cerr << "RLE decode failed or did not match expected size "
                  << (KORKEUS * LEVEYS) << " cells.\n";
        shmdt(labyrintti);
        shmctl(shmid, IPC_RMID, nullptr);
        labyrintti = nullptr;
        return 1;
    }

    // Init rats status
    for (int i = 0; i < ROTTIEN_LKMC; ++i) {
        rottienSijainnit[i] = {0, 0, false};
    }

    // Start threads (robust join only for started threads)
    pthread_t threads[ROTTIEN_LKMC];
    int ids[ROTTIEN_LKMC];
    bool started[ROTTIEN_LKMC] = {false};

    for (int i = 0; i < ROTTIEN_LKMC; ++i) {
        ids[i] = i;
        if (pthread_create(&threads[i], nullptr, aloitaRotta, &ids[i]) != 0) {
            std::cerr << "pthread_create failed for rotta " << i << "\n";
            started[i] = false;
        } else {
            started[i] = true;
        }
    }

    for (int i = 0; i < ROTTIEN_LKMC; ++i) {
        if (started[i]) pthread_join(threads[i], nullptr);
    }

    drawMaze();

    // Cleanup shared memory
    shmdt(labyrintti);
    shmctl(shmid, IPC_RMID, nullptr);
    labyrintti = nullptr;

    return 0;
}
