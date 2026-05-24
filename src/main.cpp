#include <app/App.h>

#include <signal.h>
#include <cstdio>
#include <cstdlib>

// set by signal handler; main loop checks this to break out cleanly
volatile sig_atomic_t g_quit = 0;

static void onSignal(int) { g_quit = 1; }

int main()
{
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    App app;

    app.run();

    return 0;
}
