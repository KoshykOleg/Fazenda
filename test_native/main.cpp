#include "tf.h"
#include "sim.h"
#include <cstdlib>
#include <cstring>
#include <string>

void suiteClimate();
void suiteDisplay();
void suiteLogger();
void suiteFuzz(int seeds, int hours, int firstSeed);

int main(int argc, char** argv) {
    std::string only = "all";
    int seeds = 6, hours = 48, firstSeed = 1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-v") tf::verbose = true;
        else if (a == "--echo") g_serialEcho = true;
        else if (a == "--suite" && i + 1 < argc) only = argv[++i];
        else if (a == "--seeds" && i + 1 < argc) seeds = atoi(argv[++i]);
        else if (a == "--hours" && i + 1 < argc) hours = atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) firstSeed = atoi(argv[++i]);
        else {
            printf("usage: %s [-v] [--echo] [--suite all|climate|display|logger|fuzz] [--seeds N] [--hours H] [--seed S]\n", argv[0]);
            return 2;
        }
    }
    tf::failContext = dumpSerialTail;
    if (only == "all" || only == "climate") suiteClimate();
    if (only == "all" || only == "display") suiteDisplay();
    if (only == "all" || only == "logger") suiteLogger();
    if (only == "all" || only == "fuzz") suiteFuzz(seeds, hours, firstSeed);
    return tf::summary() == 0 ? 0 : 1;
}
