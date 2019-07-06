#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"
#include "SDL2/SDL_ttf.h"
#include "SDL2/SDL2_gfxPrimitives.h"

#include "task.h"
#include "queue.h"
#include "croutine.h"

#include "TUM_Draw.h"

typedef enum{
    DRAW_CLEAR,
    DRAW_RECT,
    DRAW_CIRLE,
    DRAW_LINE,
    DRAW_POLY,
    DRAW_TRIANGLE,
    DRAW_IMAGE
}draw_job_type_t;

typedef struct rect_data{
    int x;
    int y;
    int w;
    int h;
    unsigned int colour;
}rect_data_t;

typedef struct circle_date{
    int x;
    int y;
    int radius;
    unsigned int colour;
}circle_data_t;

typedef struct line_data{
    int x1;
    int y1;
    int x2;
    int y2;
    unsigned int colour;
}line_data_t;

typedef struct poly_data{
    coord_t *points;
    unsigned int n;
    unsigned int colour;
}poly_data_t;

typedef struct triangle_data{
    coord_t *points;
    unsigned int colour;
}triangle_data_t;

typedef struct image_data{

}image_data_t;

typedef struct text_data{
    char *str;
    int x;
    int y;
    unsigned int colour;
}text_data_t;
    
union{
    rect_data_t rect;
    circle_data_t circle;
    line_data_t line;
    poly_data_t poly;
    triangle_data_t triangle;
    image_data_t image;
    text_data_t text;
} data_u;

typedef struct draw_job{
    draw_job_type_t type;
    union data_u *data; 
}draw_job_t;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
TTF_Font *font1 = NULL;
xQueueHandle drawJobQueue = NULL;
xSemaphoreHandle drawReady = NULL;

uint32_t SwapBytes(uint x)
{
    return ((x & 0x000000ff) << 24) +
           ((x & 0x0000ff00) << 8) +
           ((x & 0x00ff0000) >> 8) +
           ((x & 0xff000000) >> 24);
}

void logSDLError(char *msg)
{
    if(msg)
        printf("[ERROR] %s, %s\n", msg, SDL_GetError());
}

void logTTFError(char *msg){
    if(msg)
        printf("[ERROR] %s, %s\n", msg, TTF_GetError());
}

void vClearDisplay(void)
{
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
}

void vDrawRectangle(int x, int y, int w, int h, unsigned int colour){

    rectangleColor(renderer, x, y, x+w, y+h, SwapBytes((colour << 8) | 0xFF));
}

void vDrawCircle(int x, int y, int radius, unsigned int colour){
    if(xSemaphoreTake(DisplayReady, portMAX_DELAY) == pdTRUE){
        filledCircleColor(renderer, x, y, radius, SwapBytes((colour << 8) | 0xFF));
        xSemaphoreGive(DisplayReady);
    }
}

void vDrawLine(int x1, int y1, int x2, int y2, unsigned int colour){
    if(xSemaphoreTake(DisplayReady, portMAX_DELAY) == pdTRUE){
        lineColor(renderer, x1, y1, x2, y2, SwapBytes((colour << 8) | 0xFF)); 
        xSemaphoreGive(DisplayReady);
    }
}

void vDrawPoly(coord_t *points, unsigned int n, int colour)
{
    int16_t *x_coords = calloc(1, sizeof(int16_t) * n);
    int16_t *y_coords = calloc(1, sizeof(int16_t) * n);

    for(unsigned int i = 0; i < n; i++){
        x_coords[i] = points[i].x;
        y_coords[i] = points[i].y;
    }

    if(xSemaphoreTake(DisplayReady, portMAX_DELAY) == pdTRUE){
        polygonColor(renderer, x_coords, y_coords, n, SwapBytes((colour << 8) | 0xFF));
        xSemaphoreGive(DisplayReady);
    }

    free(x_coords);
    free(y_coords);

}

void vDrawTriangle(coord_t *points, unsigned int colour){
    if(xSemaphoreTake(DisplayReady, portMAX_DELAY) == pdTRUE){
        filledTrigonColor(renderer, points[0].x, points[0].y, points[1].x, points[1].y,
            points[2].x, points[2].y, colour << 8);
        xSemaphoreGive(DisplayReady);
    }
}

void vInitDrawing( void )
{
    int ret = 0;

    SDL_Init( SDL_INIT_EVERYTHING );

    ret = TTF_Init();

    if (ret == -1)
        logTTFError("InitDrawing->Init");

    font1 = TTF_OpenFont("fonts/IBMPlexSans-Medium.ttf", DEFAULT_FONT_SIZE);

    if(!font1)
        logTTFError("InitDrawing->OpenFont");
    
    window = SDL_CreateWindow("FreeRTOS Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

    if (!window){
        logSDLError("vInitDrawing->CreateWindow");
        SDL_Quit();
        exit(0);
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if(!renderer){
        logSDLError("vInitDrawing->CreateRenderer");
        SDL_Quit();
        exit(0);
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    SDL_RenderClear(renderer);

    drawJobQueue = xQueueCreate( 100, sizeof(job_t) );
    if(!drawJobQueue){
        printf("drawJobQueue init failed\n");
        exit(1);
    }

    xSemaphoreCreateBinary(drawReady);

    if(!drawReady){
        printf("drawReady not created\n");
        exit(1);
    }
}

SDL_Texture *loadImage(char *filename, SDL_Renderer *ren)
{
    SDL_Texture *tex = NULL;

    tex = IMG_LoadTexture(ren, filename);

    if (!tex)
    {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(window);
        logSDLError("loadImage->LoadBMP");
        SDL_Quit();
        exit(0);
    }
    
    return tex;
}

void vDrawScaledImage(SDL_Texture *tex, SDL_Renderer *ren, int x, int y, int w, int h)
{
    SDL_Rect dst;
    dst.w = w;
    dst.h = h;
    dst.x = x;
    dst.y = y;
    SDL_RenderCopy(ren, tex, NULL, &dst);
}

void vDrawImage(SDL_Texture *tex, SDL_Renderer *ren, int x, int y)
{
    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h); //Get texture dimensions
    vDrawScaledImage(tex, ren, x, y, w, h);
}

void vDrawRectImage(SDL_Texture *tex, SDL_Renderer *ren, SDL_Rect dst, SDL_Rect *clip){
    SDL_RenderCopy(ren, tex, clip, &dst);
}

void vDrawClippedImage(SDL_Texture *tex, SDL_Renderer *ren, int x, int y, SDL_Rect *clip){
    SDL_Rect dst;
    dst.x = x;
    dst.y = y;

    if(!clip){
        dst.w = clip->w;
        dst.h = clip->h;
    }else{
        SDL_QueryTexture(tex, NULL, NULL, &dst.w, &dst.h);
    }

    vDrawRectImage(tex, ren, dst, clip);
}

void vDrawText(char *string, int x, int y, unsigned int colour)
{
    stringColor(renderer, 350,350, "sup", SwapBytes((colour << 8) | 0xFF));
}

void vHandleDrawJob(job_t *job){
    if(!job) return;

    switch(job->type){
        case DRAW_CLEAR:
            vClearDisplay();
            break;
        case DRAW_RECT:
            vDrawRectange(job->data.x, job->data.y, job->data, job->data.w,
                    job->data->h, job->data.colour);
            break;
        case DRAW_CIRLE:
            vDrawRectangle(job->data.x, job->data.y, job->data radius,
                    job->data. colour);
            break;
        case DRAW_LINE:
            vDrawLine(job->data.x1, job->data.y1, job->data.x2, job->data.y2,
                    job->data.colour);
            break;
        case DRAW_POLY:
            vDrawPoly(job->data.points, job->data.n, job->data.colour);
            break;
        case DRAW_TRIANGLE:
            vDrawTriangle(job->data.points, job->data.colour);
            break;
        case DRAW_IMAGE:

            break;
        default:
            break;
        }
}

void vDrawUpdateScreen(void){
    job_t tmp_job = NULL;
    while(xQueueReceive(drawJobQueue, &tmp_job, 0) == pdTRUE)
        vHandleDrawJob(tmp_job);
    
    SDL_RenderPresent(renderer);
}

void vSetupScreen(void)
{
}

void vDrawTask ( void *pvParameters )
{
    vSetupScreen();

    while(1)
        if(xSemaphoreTake(drawReady, portMAX_DELAY) == pdTRUE)
            vDrawUpdateScreen();
}

signed char xDrawUpdateScreen(void){
    if(xSemaphoreGive(drawReady) == pdTRUE)
        return 0;

    return -1;
}

#define CREATE_JOB(TYPE) \
    TYPE##_data_t *data = calloc(1, sizeof(TYPE##_data_t));\
    if(!data) \
        logCriticalError("#TYPE data alloc");\
    job.data = data;


void logCriticalError(char *msg){
    printf("[ERROR] %s\n", msg);
    exit(-1);
}
        

signed char tumDrawBox(int x, int y, int w, int h, unsigned int colour){
    job_t job = {.type = DRAW_RECT};

    CREATE_JOB(rect);

    job.data->rect.x = x;
    job.data->rect.y = y;
    job.data->rect.w = w;
    job.data->rect.h = h;
    job.data->rect.colour = colour;

    if(xQueueSend(drawJobQueue, &job, portMAX_DELAY) != pdTRUE)
        return -1;

    return 0;
}

signed char tumDrawClear(void)
{
    job_t job = {.type = DRAW_CLEAR};

    if(xQueueSend(drawJobQueue, &job, portMAX_DELAY) != pdTRUE)
        return -1;

    return 0;
}

signed char tumDrawCircle(int x, int y, unsigned int radius, 
        unsigned int colour)
{
    job_t job = {.type = DRAW_CIRCLE};

    CREATE_JOB(circle);

    job.data->circle.x = x;
    job.data->circle.y = y;
    job.data->circle.radius = radius;
    job.data->circle.colour = colour;

    if(xQueueSend(drawJobQueue, &job, portMAX_DELAY) != pdTRUE)
        return -1;

    return 0;
}

signed char tumDrawLine(int x1, int y1, int x2, int y2, unsigned int colour)
{
    job_t job = {.type = DRAW_LINE};

    CREATE_JOB(line);

    job.data->line.x1 = x1;
    job.data->line.y1 = y1;
    job.data->line.x2 = x2;
    job.data->line.y2 = y2;
    job.data->line.colour = colour
    
    if(xQueueSend(drawJobQueue, &job, portMAX_DELAY) != pdTRUE)
        return -1;

    return 0;
}

signed char tumDrawPoly(coord_t *points, unsigned int n, unsigned int colour)
{
    job_t job = {.type = DRAW_POLY};

    CREATE_JOB(poly);

    job.data->poly.points = points;
    job.data->poly.n = n;
    job.data->poly.colour = colour;

    if(xQueueSend(drawJobQueue, &job, portMAX_DELAY) != pdTRUE)
        return -1;

    return 0;
}

signed char tumDrawTriangle(coord_t *points, unsigned int colour)
{
    job_t job = {.type = DRAW_TRIANGLE};

    CREATE_JOB(triangle);

    job.data->triangle.points = points;
    job.data->triangle.colour = colour;

    if(xQueueSend(drawJobQueue, &job, portMAX_DELAY) != pdTRUE)
        return -1;

    return 0;
}