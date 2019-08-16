#define UNICODE
#define EXPORT __declspec(dllexport)

#define ubyte unsigned char
#define uint unsigned int

/* Standard Libaries */

#include <stdio.h>
#include <functional>
#include <stdlib.h>
#include <memory>
#include <math.h>

/* Platform Libraries */

#ifdef __MINGW32__
    #pragma comment(lib, "opengl32.lib")
    #include "GL/gl.h"
    #include <windows.h>
    #include <tchar.h>
#elif __linux__
    #include <GL/glx.h>
    #include <X11/Xlib.h>
#endif

/* Structs */

struct WindowOptions
{
    const wchar_t* title;
    uint width, height, pixelSize;

    std::function<void()> onOpened;
    std::function<void()> onLoop;
    std::function<void()> onClosed;
};

struct Color
{
    ubyte r, g, b, a;
    Color() : r(0), g(0), b(0), a(0) {}
    Color(ubyte red, ubyte green, ubyte blue, ubyte alpha) :
        r(red), g(green), b(blue), a(alpha) {}
};

struct Texture
{
    std::unique_ptr<Color[]> pixels = nullptr;
    uint width = 0, height = 0;
    Texture(uint t_width, uint t_height) : width(t_width), 
        height(t_height) {
        pixels = std::make_unique<Color[]>(width * height);
    }

    Color getPixel(uint x, uint y) 
    {
        if (x >= 0 && x < width && y >= 0 && y < height)
            return pixels[y * width + x];
        return Color(0, 0, 0, 0);
    }

    void setPixel(uint x, uint y, Color color)
    {
        if (x >= 0 && x < width && y >= 0 && y < height)
            pixels[y * width + x] = color;
    }

    void fill(Color color)
    {
        for (int i = 0; i < width * height; i++) {
            pixels[i] = color;
        }
    }
};

struct Sprite
{
    uint id = 0, width = 0, height = 0;
    Sprite() {}
    Sprite(Texture& texture)
    {
        width = texture.width;
        height = texture.height;

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
            width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.pixels.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
};

/* Function Definitions */

EXPORT int createWindow(WindowOptions& options);

void mainRender();
void initDrawTarget();

/* Global Variables */

static WindowOptions options;
static Texture drawTarget(0, 0);
static Sprite dts;

/* Platform Specifics */

#ifdef __MINGW32__

    /* Variables */

    static HDC hdc;
    static HGLRC wgl_context;

    /* Required Windows Desktop Functions */

    PIXELFORMATDESCRIPTOR createPFD()
    {
        return PIXELFORMATDESCRIPTOR
        {
            .nSize = sizeof(PIXELFORMATDESCRIPTOR),
            .nVersion = 1,
            .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            .iPixelType = PFD_TYPE_RGBA,
            .cColorBits = 32,
            .cDepthBits = 24,
            .cStencilBits = 8,
            .iLayerType = PFD_MAIN_PLANE
        };
    }

    LRESULT CALLBACK WndProc(
        _In_ HWND   hWnd,
        _In_ UINT   uMsg,
        _In_ WPARAM wParam,
        _In_ LPARAM lParam
    ) {
        switch (uMsg)
        {
            case WM_CREATE: 
                {
                    auto pfd = createPFD();
                    hdc = GetDC(hWnd);
                    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
                    wgl_context = wglCreateContext(hdc);
                    wglMakeCurrent(hdc, wgl_context);

                    RECT rect;
                    GetClientRect(hWnd, &rect);
                    options.width = rect.right - rect.left;
                    options.height = rect.bottom - rect.top;

                    glEnable(GL_TEXTURE_2D);
                    initDrawTarget();

                    if (options.onOpened != nullptr)
                        options.onOpened();
                }
                break;
            case WM_PAINT: 
                {
                    SwapBuffers(hdc);
                    mainRender();

                    if (options.onLoop != nullptr)
                        options.onLoop();
                }
                break;
            case WM_DESTROY: 
                {
                    wglDeleteContext(wgl_context);
                    PostQuitMessage(0);

                    if (options.onClosed != nullptr)
                        options.onClosed();
                }
                break;
            default:
                return DefWindowProc(hWnd, uMsg, wParam, lParam);
                break;
        }

        return 0;
    }

    int createWindow(WindowOptions& t_options) 
    {
        options = t_options;
        const wchar_t CLASS_NAME[] = L"DesktopApp";
        HINSTANCE hInstance = GetModuleHandle(NULL);
        WNDCLASS wc = { };

        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpszClassName = CLASS_NAME;

        RegisterClass(&wc);

        HWND hWnd = CreateWindowEx(
            0, CLASS_NAME, options.title,
            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
            0, 0, options.width, options.height,
            NULL, NULL, hInstance, NULL 
        );

        if (!hWnd)
        {
            MessageBox(
                NULL, 
                L"Could not create Window!", 
                L"ERROR", 0
            );
            return 1;
        }

        /* Shows the Window */
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        return (int)msg.wParam;
    }

#elif __linux__

    static GLXContext glc;

    typedef GLXContext (*glXCreateContextAttribsARBProc)
        (Display*, GLXFBConfig, GLXContext, Bool, const int*);

    void mainLoop(Display* display, XID& win)
    {
        bool isRunning = true;
        XEvent event;

        while (isRunning)
        {
            if (XPending(display))
                XNextEvent(display, &event);

            switch (event.type)
            {
                case CreateNotify:
                    glEnable(GL_TEXTURE_2D);
                    initDrawTarget();
                    if (options.onOpened != nullptr)
                       options.onOpened();
                    break;

                case ClientMessage:
                    isRunning = false;
                    glXDestroyContext(display, glc);
                    XCloseDisplay(display);
                    if (options.onClosed != nullptr)
                       options.onClosed();
                    break;

                case Expose:
                    glXSwapBuffers(display, win);
                    mainRender();
                    break;
            }
        }
    }

    int createWindow(WindowOptions& t_options)
    {
        options = t_options;

        int att[] = {
            GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None
        };

        auto display = XOpenDisplay(0);

        if (display == NULL) 
        {
            printf("Cannot connect to X server!");
            return 1;
        }

        auto root = DefaultRootWindow(display);
        auto vi = glXChooseVisual(display, 0, att);

        if (vi == NULL)
        {
            printf("No visual found!");
            return 1;
        }

        auto cmap = XCreateColormap(display, root, vi->visual, AllocNone);

        XSetWindowAttributes swa;
        swa.colormap = cmap;
        swa.event_mask = ExposureMask | KeyPressMask;

        auto win = XCreateWindow(display, root, 0, 0, 
            options.width, options.height, 0, vi->depth, InputOutput,
                vi->visual, CWColormap | CWEventMask, &swa);

        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;

        hints->min_width = options.width;
        hints->max_width = options.width;
        hints->min_height = options.height;
        hints->max_height = options.height;

        XSetWMNormalHints(display, win, hints);
        XSetWMSizeHints(display, win, hints, PMinSize | PMaxSize);
        XSelectInput(display, win, ExposureMask | KeyPressMask);

        XMapWindow(display, win);
        XFlush(display);

        char cTitle[sizeof(options.title)];
        wcstombs(cTitle, options.title, sizeof(options.title));
        XStoreName(display, win, cTitle);

        Atom wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", True);
        XSetWMProtocols(display, win, &wmDelete, 1);

        glc = glXCreateContext(display, vi, NULL, GL_TRUE);
        glXMakeCurrent(display, win, glc);  

        mainLoop(display, win);

        return 0;
    }

#endif

/* Rendering */

void initDrawTarget()
{
    uint sWidth = (uint)ceilf((float)options.width / options.pixelSize);
    uint sHeight = (uint)ceilf((float)options.height / options.pixelSize);

    drawTarget = Texture(sWidth, sHeight);
    for (int i = 0; i < sWidth * sHeight; i++) 
    {
        drawTarget.pixels[i] = Color((ubyte)(rand() % 255), 
            (ubyte)(rand() % 255), (ubyte)(rand() % 255), 255);
    }

    dts = Sprite(drawTarget);
}

void mainRender()
{
    glViewport(0, 0, options.width, options.height);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glColor3f(1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, dts.id);

    glBegin(GL_QUADS);
        glVertex2f(-1, -1); glTexCoord2f(0, 0);
        glVertex2f( 1, -1); glTexCoord2f(1, 0);
        glVertex2f( 1,  1); glTexCoord2f(1, 1);
        glVertex2f(-1,  1); glTexCoord2f(0, 1);
    glEnd();

    if (options.onLoop != nullptr)
        options.onLoop();
}