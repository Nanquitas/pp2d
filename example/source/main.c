#include <3ds.h>
#include "pp2d.h"

#define MAX_SPRITES 2048

typedef struct {
    float x, y;
    float dx, dy;
    float angle;
    u32 color;
    size_t id;
} sprite_t;

static bool blending = false;
static bool rotating = false;
static bool moving = false;
static size_t n = 256;
static sprite_t sprites[MAX_SPRITES];

void simpleSpritesheetHandler(size_t i)
{
    // select the portion of the spritesheet that needs to be rendered
    pp2d_texture_select_part(0, sprites[i].x, sprites[i].y, (sprites[i].id / 2)*32, (sprites[i].id % 2)*32, 32, 32);
    if (blending)
    {
        // set a blending color
        pp2d_texture_blend(sprites[i].color);
    }
    if (rotating)
    {
        // rotate a texture of a desiderd angle
        pp2d_texture_rotate(sprites[i].angle);
    }
    // add the selected texture with all its data to pp2d's queue
    pp2d_texture_queue();
}

void initSprites(void)
{
    srand(time(NULL));
    for (size_t i = 0; i < MAX_SPRITES; i++)
    {
        sprites[i].x = rand() % (PP2D_SCREEN_TOP_WIDTH - 32);
        sprites[i].y = rand() % (PP2D_SCREEN_HEIGHT - 32);
        sprites[i].dx = rand()*4.0f/RAND_MAX - 2.0f;
        sprites[i].dy = rand()*4.0f/RAND_MAX - 2.0f;
        sprites[i].angle = rand() % 360;
        sprites[i].color = RGBA8(rand() % 0xFF, rand() % 0xFF,rand() % 0xFF,rand() % 0xFF);
        sprites[i].id = rand() & 3;
    }
}

void updateSprites(void)
{
    for (size_t i = 0; i < n; i++)
    {
        sprites[i].x += sprites[i].dx;
        sprites[i].y += sprites[i].dy;

        //check for collision with the screen boundaries
        if ((sprites[i].x < 1) || (sprites[i].x > PP2D_SCREEN_TOP_WIDTH - 32))
        {
            sprites[i].dx = -sprites[i].dx;
        }

        if ((sprites[i].y < 1) || (sprites[i].y > PP2D_SCREEN_HEIGHT - 32))
        {
            sprites[i].dy = -sprites[i].dy;
        }

        sprites[i].angle++;
    }
}

int main()
{
    // init example variables
    initSprites();

    // since the spritesheet is in the romfs, init it
    romfsInit();
    
    // init pp2d environment
    pp2d_init();
    
    // load the spritesheet from romfs
    pp2d_load_texture_png(0, "romfs:/ballsprites.png");
    
    // set the screen background color
    pp2d_set_screen_color(GFX_TOP, ABGR8(255, 10, 10, 10));
    
    while (aptMainLoop() && !(hidKeysDown() & KEY_START))
    {
        // read inputs
        touchPosition touch;
        hidScanInput();
        hidTouchRead(&touch);

        if ((hidKeysHeld() & KEY_UP) && n < MAX_SPRITES) n++;
        else if ((hidKeysHeld() & KEY_DOWN) && n > 1) n--;
        
        if (hidKeysDown() & KEY_TOUCH && touch.px >= 20 && touch.px <= 100 && touch.py >= 160 && touch.py <= 210) blending = !blending;
        else if (hidKeysDown() & KEY_TOUCH && touch.px >= 120 && touch.px <= 200 && touch.py >= 160 && touch.py <= 210) rotating = !rotating;
        else if (hidKeysDown() & KEY_TOUCH && touch.px >= 220 && touch.px <= 300 && touch.py >= 160 && touch.py <= 210) moving = !moving;
        
        if (moving)
        {
            updateSprites(); 
        }

        //begin a frame. this needs to be called once per frame, not once per screen
        pp2d_frame_begin(GFX_TOP, GFX_LEFT);
            // draw our sprites
            for (size_t i = 0; i < n; i++)
            {
                simpleSpritesheetHandler(i);
            }

        // change screen
        pp2d_frame_draw_on(GFX_BOTTOM, GFX_LEFT);
            // draws a rectangle
            pp2d_draw_rectangle(0, 0, PP2D_SCREEN_BOTTOM_WIDTH, PP2D_SCREEN_HEIGHT, RGBA8(0x20, 0x20, 0x20, 0xFF));

            pp2d_draw_rectangle(20, 160, 80, 50, blending ? RGBA8(0, 0xFF, 0, 0xFF) : RGBA8(0xFE, 0xFE, 0xFE, 0xFF));
            pp2d_draw_rectangle(120, 160, 80, 50, rotating ? RGBA8(0, 0xFF, 0, 0xFF) : RGBA8(0xFE, 0xFE, 0xFE, 0xFF));
            pp2d_draw_rectangle(220, 160, 80, 50, moving ? RGBA8(0, 0xFF, 0, 0xFF) : RGBA8(0xFE, 0xFE, 0xFE, 0xFF));
            // draws text
            pp2d_draw_text(33, 178, 0.5f, 0.5f, blending ? RGBA8(0, 0, 0, 0xFF) : RGBA8(0, 0, 0, 0xFF), "Blending");
            pp2d_draw_text(135, 178, 0.5f, 0.5f, rotating ? RGBA8(0, 0, 0, 0xFF) : RGBA8(0, 0, 0, 0xFF), "Rotating");
            pp2d_draw_text(237, 178, 0.5f, 0.5f, moving ? RGBA8(0, 0, 0, 0xFF) : RGBA8(0, 0, 0, 0xFF), "Moving");

            const float h = pp2d_get_text_height("a", 0.5f, 0.5f);
            pp2d_draw_textf(2, 2, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "Rendering %d/%d sprites in VBO mode", n, MAX_SPRITES);
            pp2d_draw_text(2, 2 + h, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "CPU:");
            pp2d_draw_text(2, 2 + h*2, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "GPU:");
            pp2d_draw_text(2, 2 + h*3, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "CmdBuf:");
            pp2d_draw_textf(60, 2 + h, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "%2.2f%%", C3D_GetProcessingTime()*6.0f);
            pp2d_draw_textf(60, 2 + h*2, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "%2.2f%%", C3D_GetDrawingTime()*6.0f);
            pp2d_draw_textf(60, 2 + h*3, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "%2.2f%%", C3D_GetCmdBufUsage()*100.0f);
            pp2d_draw_text(2, 2 + h*5, 0.5f, 0.5f, RGBA8(0xFE, 0xFE, 0xFE, 0xFF), "Press UP/DOWN to add/remove sprites");
        
        // ends a frame. this needs to be called once per frame, not once per screen
        pp2d_frame_end();
    }

    // exit pp2d environment
    pp2d_exit();
    
    return 0;
}