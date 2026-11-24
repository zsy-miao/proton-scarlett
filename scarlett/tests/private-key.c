/* External controller, run only by the isolated launcher, in its Wine prefix. */
#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    static const struct { const wchar_t *name; WORD vk; } keys[] = {
        {L"space", VK_SPACE}, {L"w",'W'}, {L"a",'A'}, {L"s",'S'}, {L"d",'D'},
        {L"BackSpace",VK_BACK}, {L"Return",VK_RETURN},
        {L"Up",VK_UP}, {L"Down",VK_DOWN}, {L"Left",VK_LEFT}, {L"Right",VK_RIGHT}
    };
    if (argc != 4) return 1;
    /* Refuse stand-alone invocation on Windows or a user's normal Wine desktop. */
    FILE *init = _wfopen(L"Z:\\proc\\1\\comm",L"r");
    char comm[16] = {0};
    if (!init) return 1;
    BOOL private_init = fgets(comm,sizeof(comm),init) && !strcmp(comm,"bwrap\n");
    fclose(init);
    if (!private_init) return 1;
    wchar_t *end, title[512], path[MAX_PATH];
    long ms = wcstol(argv[3], &end, 10);
    if (*end || ms < 1 || ms > 2000) return 1;
    WORD vk = 0;
    for (unsigned i=0; i<ARRAYSIZE(keys); i++)
        if (!wcscmp(argv[2],keys[i].name)) vk=keys[i].vk;
    HWND w = GetForegroundWindow();
    DWORD pid, n=ARRAYSIZE(path);
    if (!vk || !w || !GetWindowTextW(w,title,ARRAYSIZE(title)) || wcscmp(title,argv[1])
            || !GetWindowThreadProcessId(w,&pid)) return 1;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!p) return 1;
    BOOL ok = QueryFullProcessImageNameW(p,0,path,&n);
    CloseHandle(p);
    const wchar_t *base = ok ? wcsrchr(path,L'\\') : NULL;
    if (!base || _wcsicmp(base+1,L"game_scarlett_master.exe") || GetForegroundWindow()!=w) return 1;
    INPUT input = {.type=INPUT_KEYBOARD, .ki={.wVk=vk}};
    if (SendInput(1,&input,sizeof(input))!=1) return 1;
    Sleep(ms);
    HWND release_window = GetForegroundWindow();
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(1,&input,sizeof(input))!=1) return 1;
    DWORD release_pid = 0;
    if (release_window!=w || GetForegroundWindow()!=w ||
            !GetWindowThreadProcessId(w,&release_pid) || release_pid!=pid) {
        wprintf(L"hold unconfirmed: focus changed; key-up sent for %ls\n",argv[2]);
        return 0;
    }
    wprintf(L"input window hwnd=%p pid=%lu at press and release\n",(void*)w,pid);
    wprintf(L"private Wine pid=%lu: %ls held %ld ms\n",pid,argv[2],ms);
    return 0;
}
