#include <windows.h>
#include <stdio.h>

static DWORD WINAPI loader(LPVOID p)
{
    HMODULE h = LoadLibraryA((const char *)p);
    printf("LoadLibrary(%s) = %p err=%lu\n", (const char *)p, (void *)h, GetLastError());
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        HANDLE t = CreateThread(NULL, 0, loader, argv[i], 0, NULL);
        if (WaitForSingleObject(t, 8000) == WAIT_TIMEOUT) {
            printf("TIMEOUT loading %s\n", argv[i]);
            fflush(stdout);
            ExitProcess(2);
        }
        CloseHandle(t);
    }
    return 0;
}
