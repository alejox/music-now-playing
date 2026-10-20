// Ejecutable principal de "OBS Now Playing.app".
//
// macOS espera que el ejecutable de una app sea un programa compilado: con un
// script en su lugar, abrir la app puede fallar con "no se puede abrir (-47)".
// Este lanzador hace lo mismo que hacía el script: arranca el node oficial que
// viene en Resources con server.js, en segundo plano, y termina. Así, abrir la
// app otra vez lo vuelve a correr y server.js muestra el panel de la copia abierta.
//
// Se compila sin el SDK de Apple (ver build/build.sh): por eso declaramos a mano
// las pocas funciones de libSystem que usa.

typedef int pid_t;
int _NSGetExecutablePath(char *buf, unsigned int *size);
pid_t fork(void);
pid_t setsid(void);
int open(const char *path, int flags, ...);
int dup2(int from, int to);
int execv(const char *path, char *const argv[]);
__attribute__((noreturn)) void _exit(int status);

#define O_RDWR 2
#define MAX 4096

// dst = dir + suffix; devuelve 0 si no cabe.
static int join(char *dst, const char *dir, int dirlen, const char *suffix) {
  int i = 0;
  for (; i < dirlen; i++) dst[i] = dir[i];
  for (int j = 0; suffix[j]; j++, i++) {
    if (i >= MAX - 1) return 0;
    dst[i] = suffix[j];
  }
  dst[i] = 0;
  return 1;
}

int main(void) {
  char exe[MAX];
  unsigned int size = sizeof exe;
  if (_NSGetExecutablePath(exe, &size) != 0) return 1;

  // .../OBS Now Playing.app/Contents/MacOS/OBS Now Playing → .../Contents/MacOS
  int dirlen = 0;
  for (int i = 0; exe[i]; i++) if (exe[i] == '/') dirlen = i;
  if (dirlen == 0) return 1;

  char node[MAX], script[MAX];
  if (!join(node, exe, dirlen, "/../Resources/node")) return 1;
  if (!join(script, exe, dirlen, "/../Resources/app/server.js")) return 1;

  pid_t pid = fork();
  if (pid < 0) return 1;
  if (pid > 0) return 0; // la app "termina" y el servidor queda corriendo

  setsid();
  int devnull = open("/dev/null", O_RDWR);
  if (devnull >= 0) {
    dup2(devnull, 0);
    dup2(devnull, 1);
    dup2(devnull, 2);
  }
  char *argv[] = {node, script, "--open", "--log", 0};
  execv(node, argv);
  _exit(127);
}
