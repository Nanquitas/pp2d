#include <3ds.h>
#include "pp2d.h"

#define MAX_SPRITES 2048
#define BALL_WIDTH 32
#define BALL_HEIGHT 32

// A struct representing a ball
typedef struct
{
    PP2D_Sprite *sprite;
    float       velocityX;
    float       velocityY;
    float       rotationalSpeed;
} ball_t;

static bool     g_outline = false;
static bool     g_rotating = false;
static bool     g_moving = false;
static float    g_thickness = 10.f;
static float    g_scaling = 0.f;
static size_t   g_n = 5;



void initBalls(PP2D_TexRef texture, ball_t *ball)
{
    // Init random seed
    srand(time(NULL));

    // Init all balls
    for (u32 i = 0; i < MAX_SPRITES; ++i, ++ball)
    {
        u32     id = rand() % 4;
        float   posX = rand() % (PP2D_SCREEN_TOP_WIDTH - BALL_WIDTH);
        float   posY = rand() % (PP2D_SCREEN_HEIGHT - BALL_HEIGHT);
        float   texLeft = (id / 2 * BALL_WIDTH);
        float   texTop = (id % 2 * BALL_HEIGHT);
        float   texRight = texLeft + BALL_WIDTH;
        float   texBottom = texTop + BALL_HEIGHT;
        float   scale = ((float)(rand() % 100) - 50.f) / 100.f;

        ball->sprite = pp2d_new_sprite_textured(posX, posY, texture, (PP2D_TexCoords){ texLeft, texTop, texRight, texBottom });
        ball->velocityX = rand() % 100;
        ball->velocityY = rand() % 100;
        ball->rotationalSpeed = (float)(rand() % 4) * 360.f;

        pp2d_sprite_scale(ball->sprite, scale, scale);
    }
}

void    updateBalls(const float delta, ball_t *ball)
{
    for (u32 i = 0; i < g_n; ++i, ++ball)
    {
        PP2D_Sprite *sprite = ball->sprite;

        if (g_moving)
        {
            // Check for collision with the screen boundaries
            if ((sprite->posX < 1.f) || (sprite->posX > PP2D_SCREEN_TOP_WIDTH - sprite->width))
                ball->velocityX = -ball->velocityX;

            if ((sprite->posY < 1.f) || (sprite->posY > PP2D_SCREEN_HEIGHT - sprite->height))
                ball->velocityY = -ball->velocityY;

            // Move the sprite
            pp2d_sprite_move(sprite, ball->velocityX * delta, ball->velocityY * delta);

        }

        if (g_rotating)
            pp2d_sprite_rotate(sprite, ball->rotationalSpeed * delta);

        if (g_scaling != 0.f)
            pp2d_sprite_scale(sprite, g_scaling, g_scaling);

        pp2d_sprite_update(sprite);
    }
}

void    drawBalls(ball_t *ball)
{
    if (!g_outline)
    {
        // Draw our objects
        for (u32 i = 0; i < g_n; ++i, ++ball)
            pp2d_sprite_draw(ball->sprite);
    }
    else
    {
        // Outline color
        PP2D_Color color = { .a = 255, .r = 0, .g = 0, .b = 255};

        // Enable shape outlining
        pp2d_shape_outlining_begin();

        // Draw our objects
        for (u32 i = 0; i < g_n; ++i)
            pp2d_sprite_draw(ball[i].sprite);

        // Define the outline's settings
        pp2d_shape_outlining_apply(color, g_thickness);

        // Draw our objects again (will actually draw the outline)
        for (u32 i = 0; i < g_n; ++i, ++ball)
            pp2d_sprite_draw(ball->sprite);

        // End shape outlining
        pp2d_shape_outlining_end();
    }
} 

float   GetTimeAsSeconds(void)
{
    #define TICKS_PER_SEC 268123480

    return (float)((float)svcGetSystemTick()/(float)TICKS_PER_SEC);
}

int     main()
{
    // since the spritesheet is in the romfs, init it
    romfsInit();
    
    // init pp2d environment
    pp2d_init();

    // Load the texture
    PP2D_TexRef     texture = pp2d_texture_from_png("romfs:/ballsprites.png");
    ball_t  *       balls = (ball_t *)malloc(sizeof(ball_t) * MAX_SPRITES);

    // Init all balls
    initBalls(texture, balls);
    
    // set the screen background color
    pp2d_set_screen_color(GFX_TOP, ABGR8(255, 10, 10, 10));
    pp2d_set_screen_color(GFX_BOTTOM, ABGR8(255, 0x20, 0x20, 0x20));

    const float h = pp2d_get_text_height("a", 0.5f, 0.5f);

    float delta;
    float lastTime = GetTimeAsSeconds();

    while (aptMainLoop() && !(hidKeysDown() & KEY_START))
    {
        delta = lastTime;
        lastTime = GetTimeAsSeconds();
        delta = lastTime - delta;

        // read inputs
        touchPosition touch;
        hidScanInput();
        hidTouchRead(&touch);

        if ((hidKeysHeld() & KEY_UP) && g_n < MAX_SPRITES) ++g_n;
        else if ((hidKeysHeld() & KEY_DOWN) && g_n > 1) --g_n;
        else if ((hidKeysHeld() & KEY_LEFT)) g_thickness -= 1.f;
        else if ((hidKeysHeld() & KEY_RIGHT)) g_thickness += 1.f;

        if (hidKeysHeld() & KEY_X) g_scaling = 0.01f;
        else if (hidKeysHeld() & KEY_Y) g_scaling = -0.01f;
        else g_scaling = 0.f;
        
        if (hidKeysDown() & KEY_TOUCH && touch.px >= 20 && touch.px <= 100 && touch.py >= 160 && touch.py <= 210) g_outline = !g_outline;
        else if (hidKeysDown() & KEY_TOUCH && touch.px >= 120 && touch.px <= 200 && touch.py >= 160 && touch.py <= 210) g_rotating = !g_rotating;
        else if (hidKeysDown() & KEY_TOUCH && touch.px >= 220 && touch.px <= 300 && touch.py >= 160 && touch.py <= 210) g_moving = !g_moving;
        
        // Update balls (moving, rotation etc)
        updateBalls(delta, balls);

        // Begin a frame. this needs to be called once per frame, not once per screen
        pp2d_frame_begin(GFX_TOP, GFX_LEFT);

        // Draw our balls
        drawBalls(balls);

        // change screen
        pp2d_frame_draw_on(GFX_BOTTOM, GFX_LEFT);

            // draws a rectangle
            pp2d_draw_rectangle(20, 160, 80, 50, g_outline ? RGBA8(0, 0xFF, 0, 0xFF) : RGBA8(0xFE, 0xFE, 0xFE, 0xFF));
            pp2d_draw_rectangle(120, 160, 80, 50, g_rotating ? RGBA8(0, 0xFF, 0, 0xFF) : RGBA8(0xFE, 0xFE, 0xFE, 0xFF));
            pp2d_draw_rectangle(220, 160, 80, 50, g_moving ? RGBA8(0, 0xFF, 0, 0xFF) : RGBA8(0xFE, 0xFE, 0xFE, 0xFF));
            // draws text
            pp2d_draw_text(33, 178, 0.5f, 0.5f, g_outline ? RGBA8(0, 0, 0, 0xFF) : RGBA8(0, 0, 0, 0xFF), "Outlining");
            pp2d_draw_text(135, 178, 0.5f, 0.5f, g_rotating ? RGBA8(0, 0, 0, 0xFF) : RGBA8(0, 0, 0, 0xFF), "Rotating");
            pp2d_draw_text(237, 178, 0.5f, 0.5f, g_moving ? RGBA8(0, 0, 0, 0xFF) : RGBA8(0, 0, 0, 0xFF), "Moving");

            pp2d_draw_textf(2, 2, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "Rendering %d/%d sprites in VBO mode", g_n, MAX_SPRITES);
            pp2d_draw_text(2, 2 + h, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "CPU:");
            pp2d_draw_text(2, 2 + h*2, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "GPU:");
            pp2d_draw_text(2, 2 + h*3, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "CmdBuf:");
            pp2d_draw_textf(60, 2 + h, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "%2.2f%%", C3D_GetProcessingTime()*6.0f);
            pp2d_draw_textf(60, 2 + h*2, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "%2.2f%%", C3D_GetDrawingTime()*6.0f);
            pp2d_draw_textf(60, 2 + h*3, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "%2.2f%%", C3D_GetCmdBufUsage()*100.0f);
            pp2d_draw_text(2, 2 + h*5, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "Press UP/DOWN to add/remove sprites");
            pp2d_draw_text(2, 2 + h*6, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "Press LEFT/RIGHT to incr./decr. outline's thickness");
            pp2d_draw_text(2, 2 + h*7, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "Press Y/X to upscale/downscale sprites");

        // ends a frame. this needs to be called once per frame, not once per screen
        pp2d_frame_end();
    }

    // Destroy sprites and release resources
    for (u32 i = 0; i < MAX_SPRITES; ++i)
        pp2d_destroy_sprite(balls[i].sprite);
    free(balls);

    // Destroy texture and release resources
    pp2d_destroy_texture(texture);

    // exit pp2d environment
    pp2d_exit();
    
    return 0;
}
