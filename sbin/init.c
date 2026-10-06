#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/vt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

static void mount_virtual_fs(const char *path, const char *type, mode_t mode)
{
    if (mkdir(path, mode) == -1 && errno != EEXIST) {
        perror(path);
        return;
    }

    if (mount(type, path, type, 0, NULL) == -1 && errno != EBUSY)
        perror(path);
}

static void init_base_virtual_fs(void)
{
    /* O kernel monta a raiz como somente leitura por padrao. */
    if (mount(NULL, "/", NULL, MS_REMOUNT, NULL) == -1) {
        perror("remount /");
    }

    mount_virtual_fs("/proc", "proc", 0755);
    mount_virtual_fs("/sys", "sysfs", 0755);
    mount_virtual_fs("/dev", "devtmpfs", 0755);
    mount_virtual_fs("/tmp", "tmpfs", 01777);
    /* O udev guarda seu banco de dados em /run/udev, que nao pode sobreviver
     * a reinicializacoes. */
    mount_virtual_fs("/run", "tmpfs", 0755);
}

static void config_fb_screen(void)
{
    int fd = open("/dev/tty0", O_RDWR | O_NOCTTY | O_CLOEXEC);

    if (fd == -1) {
        perror("/dev/tty0");
        return;
    }

    /* Bloqueio global: Ctrl+Alt+Fn nao pode trocar o VT ativo. */
    if (ioctl(fd, VT_LOCKSWITCH, 0) == -1)
        perror("VT_LOCKSWITCH");

    close(fd);
}

static int read_number(const char *path, long *value)
{
    FILE *file = fopen(path, "r");
    int success;

    if (file == NULL) {
        return 0;
    }

    success = fscanf(file, "%ld", value) == 1;
    fclose(file);
    return success;
}

static void print_cpu(void)
{
    FILE *file = fopen("/proc/cpuinfo", "r");
    char line[512];
    int found = 0;

    if (file == NULL) {
        perror("/proc/cpuinfo");
        puts("CPU: indisponivel");
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *colon = strchr(line, ':');
        char *value;
        size_t key_length;
        if (colon == NULL)
            continue;
        // Cortando a string, apenas o que tem antes do : importa para determinar label
        *colon = '\0';
        // Removendo espaços em branco e tabs
        key_length = strlen(line);
        while (
                key_length > 0 &&
               (line[key_length - 1] == ' ' || line[key_length - 1] == '\t')
            ) {
            line[--key_length] = '\0';
        }
        // Se não é a label que procuramos, pule
        if (strcmp(line, "Processor") != 0 && strcmp(line, "model name") != 0)
            continue;
        // Encontramos a label correta, obtendo valor
        value = colon + 1;
        value += strspn(value, "\t ");
        value[strcspn(value, "\r\n")] = '\0';
        if (*value != '\0') {
            printf("CPU: %s\n", value);
            found = 1;
            break;
        }
    }
    fclose(file);

    if (!found)
        puts("CPU: indisponivel");
}

static void print_hardware_info(void)
{
    struct utsname kernel;
    struct sysinfo memory;
    long value;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);

    if (uname(&kernel) == 0) {
        printf("Arquitetura: %s\n", kernel.machine);
        printf("Kernel: %s %s\n", kernel.sysname, kernel.release);
    } else {
        perror("uname");
    }

    print_cpu();
    printf("Nucleos online: %ld\n", cores);

    if (read_number("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &value)
        && value > 0)
        printf("Frequencia da CPU (cpu0): %.1f MHz\n", value / 1000.0);
    else
        puts("Frequencia da CPU: indisponivel");

    if (sysinfo(&memory) == 0) {
        double unit_mib = memory.mem_unit / (1024.0 * 1024.0);
        printf("RAM reconhecida pelo kernel: %.1f MiB\n",
               memory.totalram * unit_mib);
        printf("RAM livre (exclui cache): %.1f MiB\n",
               memory.freeram * unit_mib);
    } else {
        perror("sysinfo");
    }
    puts("------------------------------\n");
}

static void print_random_number(void)
{
    FILE *file = fopen("/dev/random", "rb");
    unsigned int number;

    if (file == NULL) {
        perror("/dev/random");
        return;
    }

    if (fread(&number, sizeof(number), 1, file) == 1)
        printf("Numero aleatorio: %u\n", number);
    else
        fputs("Falha ao ler /dev/random\n", stderr);

    fclose(file);
}

static void draw_purple_rectangle(void)
{
    struct fb_var_screeninfo screen;
    struct fb_fix_screeninfo fixed;
    uint32_t pixel = 0;
    unsigned int bytes, x, y;
    unsigned char *buffer;
    int fd = open("/dev/fb0", O_RDWR);

    if (fd == -1) {
        perror("/dev/fb0");
        return;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &screen) == -1 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fixed) == -1) {
        perror("Informacoes de /dev/fb0");
        goto done;
    }
    printf("Tela: %ux%u, %u bits por pixel\n"
           "Resolucao virtual: %ux%u, deslocamento: %u,%u, linha: %u bytes\n",
           screen.xres, screen.yres, screen.bits_per_pixel,
           screen.xres_virtual, screen.yres_virtual,
           screen.xoffset, screen.yoffset, fixed.line_length);

    if (fixed.type != FB_TYPE_PACKED_PIXELS || fixed.visual != FB_VISUAL_TRUECOLOR ||
        screen.nonstd || screen.grayscale || screen.xres < 200 || screen.yres < 200 ||
        (screen.bits_per_pixel != 16 && screen.bits_per_pixel != 24 &&
         screen.bits_per_pixel != 32)) {
        fputs("Framebuffer: formato ou tamanho nao suportado\n", stderr);
        goto done;
    }
    /* Roxo RGB(128, 0, 128), adaptado aos campos de cor do framebuffer. */
    struct fb_bitfield fields[] = {screen.red, screen.green, screen.blue, screen.transp};
    const unsigned int colors[] = {128, 0, 128, 255};
    for (unsigned int i = 0; i < 4; i++) {
        if (fields[i].length > 32 || fields[i].offset > 32 ||
            fields[i].length + fields[i].offset > screen.bits_per_pixel ||
            fields[i].msb_right) {
            fputs("Framebuffer: campos de cor nao suportados\n", stderr);
            goto done;
        }
        if (fields[i].length) {
            pixel |= (uint32_t)((((UINT64_C(1) << fields[i].length) - 1) *
                                colors[i] / 255) << fields[i].offset);
        }
    }
    bytes = screen.bits_per_pixel / 8;
    x = (screen.xres - 50) / 2;
    y = (screen.yres - 50) / 2;
    uint64_t right = ((uint64_t)screen.xoffset + x + 50) * bytes;
    uint64_t last_row = (uint64_t)screen.yoffset + y + 49;
    if (right > fixed.line_length || last_row * fixed.line_length + right > fixed.smem_len) {
        fputs("Framebuffer: retangulo fora da memoria disponivel\n", stderr);
        goto done;
    }
    buffer = mmap(NULL, fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap /dev/fb0");
        goto done;
    }
    /* Banana Pi usa little-endian; respeite o stride e os offsets virtuais. */
    for (unsigned int row = 0; row < 50; row++) {
        for (unsigned int col = 0; col < 50; col++) {
            memcpy(buffer + (size_t)(screen.yoffset + y + row) * fixed.line_length +
                   (size_t)(screen.xoffset + x + col) * bytes, &pixel, bytes);
        }
    }
    munmap(buffer, fixed.smem_len);
done:
    close(fd);
}

static int run_and_wait(char *const argv[])
{
    int status;
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%s falhou\n", argv[0]);
        return -1;
    }
    return 0;
}

static void start_udev(void)
{
    char *const udevd[] = {"/sbin/udevd", "--daemon", NULL};
    char *const subsystems[] = {"/usr/bin/udevadm", "trigger",
                                "--type=subsystems", "--action=add", NULL};
    char *const devices[] = {"/usr/bin/udevadm", "trigger",
                             "--type=devices", "--action=add", NULL};
    char *const settle[] = {"/usr/bin/udevadm", "settle", "--timeout=30", NULL};

    if (access(udevd[0], X_OK) == -1) {
        puts("udevd nao encontrado, seguindo sem udev");
        return;
    }
    /* Com --daemon, o udevd so retorna depois de abrir o socket de eventos do
     * kernel, entao nenhum evento disparado a seguir se perde. O processo que
     * fica em segundo plano passa a ser filho do init. */
    if (run_and_wait(udevd) == -1)
        return;
    /* Os dispositivos detectados durante o boot geraram eventos antes de o
     * udevd existir. O trigger pede ao kernel para reenvia-los, e o settle
     * espera o udevd processar todos, como o S10udevd do Buildroot faz. */
    if (run_and_wait(subsystems) == -1 || run_and_wait(devices) == -1)
        return;
    run_and_wait(settle);
    puts("udev iniciado");
}

static pid_t start_program(void)
{
    if (access("/bin/start", F_OK) == -1) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork /bin/start");
    } else if (pid == 0) {
        puts("Binario /bin/start encontrado, iniciando...");
        execl("/bin/start", "/bin/start", (char *)NULL);
        perror("exec /bin/start");
        _exit(127);
    }
    return pid;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("Seu Banana Pi completou o boot do Kernel Linux!");

    init_base_virtual_fs();
    config_fb_screen();
    print_hardware_info();
    print_random_number();
    draw_purple_rectangle();
    start_udev();
    pid_t start_pid = start_program();

    for (;;) {
        int status;
        pid_t pid = wait(&status);
        if (pid == -1) {
            if (errno == EINTR)
                continue;
            if (errno != ECHILD)
                perror("wait");
            /* Sem filhos, evite consumir CPU enquanto mantem o init vivo. */
            sleep(1);
        } else if (pid == start_pid) {
            if (WIFEXITED(status))
                printf("\n/bin/start finalizou com codigo de saida %d\n",
                       WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                printf("\n/bin/start finalizou pelo sinal %d\n",
                       WTERMSIG(status));
            start_pid = -1;
        }
    }
}
