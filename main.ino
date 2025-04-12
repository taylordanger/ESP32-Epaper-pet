/* Includes ------------------------------------------------------------------*/
#include <Arduino.h>
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"
#include "imagedata.h"

/* Pin Definitions -----------------------------------------------------------*/
#define MOSI            14
#define RST             32
#define BUSY            35
#define CS              15
#define CLK             13
#define DC              33
#define PIN_BUTTON      16  // Main button (select)
#define PIN_BUTTON_SCROLL 17 // Scroll button

/* Constants -----------------------------------------------------------------*/
#define SCREEN_WIDTH    200
#define SCREEN_HEIGHT   200
#define SCORE_HEIGHT    24
#define TIME_HEIGHT     20
#define UPDATE_INTERVAL 1000  // Time update interval (1 second)
#define FRAME_DELAY     100   // Animation frame rate
#define BATTERY_DRAIN   60    // Seconds between battery decrements
#define MENU_PADDING    10    // Menu padding
#define MENU_WIDTH      160   // Menu width
#define MENU_HEIGHT     160   // Menu height
#define ANIMATION_CYCLES   2    // Number of animation cycles to play at startup
#define STATE_ANIMATING    0    // Pet is animating
#define STATE_FROZEN       1    // Pet is frozen on a frame
#define STATE_MENU         2    // In menu system


// Menu states
#define MENU_STATE_NONE     0
#define MENU_STATE_MAIN     1
#define MENU_STATE_SET_TIME 2

// Menu items
#define MENU_ITEM_RESET_SCORE  0
#define MENU_ITEM_RESET_BATTERY 1
#define MENU_ITEM_SET_TIME     2
#define MENU_ITEM_EXIT         3
#define MENU_ITEM_COUNT        4

// Button handling
#define BUTTON_DEBOUNCE_MS   1
#define DOUBLE_CLICK_TIME_MS 2000

/* Types ---------------------------------------------------------------------*/
// Animation sequence structure
typedef struct {
    const UBYTE** frames;
    uint8_t num_frames;
} Animation;

// Time setting structure
typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool is_pm;
    uint8_t editing; // 0=hour, 1=minute, 2=am/pm, 3=done
} TimeSettings;

// Game state structure
typedef struct {
    PAINT_TIME current_time;
    uint16_t score;
    uint8_t battery;
    bool button_pressed;
    unsigned long last_time_update;
    unsigned long last_frame_time;
    uint8_t current_frame;
    
    // Menu system
    uint8_t menu_state;
    uint8_t menu_selection;
    TimeSettings time_settings;
    
    // Button handling
    unsigned long last_button_press;
    unsigned long last_scroll_press;
    uint8_t click_count;
    uint8_t pet_state;           // Current pet state (animating/frozen)
    uint8_t animation_cycles;    // Count of completed animation cycles
} GameState;

/* Global Variables ----------------------------------------------------------*/
UBYTE* display_buffer = NULL;
GameState state;

// Animation sequences
const UBYTE* normal_frames[] = {
    screen_10happy_1,
    screen_11happy_2,
    screen_12happy_2_1,
    screen_13happy_3,
    screen_14happy_4,
    screen_10happy_1
};

const UBYTE* button_frames[] = {
    screen_1hungry_1_1,
    screen_1hungry_1_1,
    screen_1hungry_1_1
};

const Animation normal_anim = {normal_frames, 6};
const Animation button_anim = {button_frames, 3};

// Menu text options
const char* menu_items[] = {
    "Reset Score",
    "Reset Battery",
    "Set Time",
    "Exit Menu"
};

/* Function Prototypes -------------------------------------------------------*/
void update_time(PAINT_TIME* time);
void draw_interface(GameState* state);
void draw_menu(GameState* state);
void draw_time_setting_menu(GameState* state);
bool init_hardware(void);
void cleanup(void);
void handle_buttons(void);
void handle_menu_actions(void);
void convert_time_settings_to_paint_time(TimeSettings* settings, PAINT_TIME* paint_time);

/* Functions -----------------------------------------------------------------*/
// Update time keeping
void update_time(PAINT_TIME* time) {
    time->Sec++;
    if (time->Sec >= 60) {
        time->Sec = 0;
        time->Min++;
        if (time->Min >= 60) {
            time->Min = 0;
            time->Hour++;
            if (time->Hour >= 24) {
                time->Hour = 0;
            }
        }
    }
}

//// Convert 12-hour time settings to 24-hour PAINT_TIME
//void convert_time_settings_to_paint_time(TimeSettings* settings, PAINT_TIME* paint_time) {
//    // Convert from 12-hour to 24-hour format
//    paint_time->Hour = settings->hour;
//    if (settings->is_pm && settings->hour != 12) {
//        paint_time->Hour += 12;
//    } else if (!settings->is_pm && settings->hour == 12) {
//        paint_time->Hour = 0;
//    }
//    
//    paint_time->Min = settings->minute;
//    paint_time->Sec = 0;
//}

// Draw game interface elements
void draw_interface(GameState* state) {
    // Draw score and battery bar
    Paint_DrawRectangle(0, 0, SCREEN_WIDTH, SCORE_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %d  %d%%", state->score, state->battery);
    Paint_DrawString_EN(0, 3, score_str, &Font20, BLACK, WHITE);

    // Draw time bar
    Paint_DrawRectangle(0, SCREEN_HEIGHT - TIME_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT, 
                      BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    
    // Create a copy of current_time to pass to Paint_DrawTime since it's non-const
    PAINT_TIME time_copy = state->current_time;
    Paint_DrawTime(25, SCREEN_HEIGHT - TIME_HEIGHT, &time_copy, &Font20, BLACK, WHITE);
}

// Draw main menu
void draw_menu(GameState* state) {
    int x = (SCREEN_WIDTH - MENU_WIDTH) / 2;
    int y = (SCREEN_HEIGHT - MENU_HEIGHT) / 2;
    
    // Draw menu background
    Paint_DrawRectangle(x, y, x + MENU_WIDTH, y + MENU_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(x + 1, y + 1, x + MENU_WIDTH - 1, y + MENU_HEIGHT - 1, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    
    // Draw title
    Paint_DrawString_EN(x + MENU_PADDING, y + MENU_PADDING, "MENU", &Font20, BLACK, WHITE);
    Paint_DrawLine(x + MENU_PADDING, y + MENU_PADDING + 25, x + MENU_WIDTH - MENU_PADDING, 
                  y + MENU_PADDING + 25, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    // Draw menu items
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (i == state->menu_selection) {
            // Highlight selected item
            Paint_DrawRectangle(x + MENU_PADDING, y + MENU_PADDING + 35 + i * 25, 
                              x + MENU_WIDTH - MENU_PADDING, y + MENU_PADDING + 35 + (i + 1) * 25 - 5, 
                              BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
            Paint_DrawString_EN(x + MENU_PADDING + 5, y + MENU_PADDING + 35 + i * 25 + 2, 
                              menu_items[i], &Font16, WHITE, BLACK);
        } else {
            Paint_DrawString_EN(x + MENU_PADDING + 5, y + MENU_PADDING + 35 + i * 25 + 2, 
                              menu_items[i], &Font16, BLACK, WHITE);
        }
    }
    
    // Draw navigation hint
    Paint_DrawString_EN(x + MENU_PADDING, y + MENU_HEIGHT - 25, 
                      "Btn0: Scroll  Btn16: Select", &Font8, BLACK, WHITE);
}

// Draw time setting menu
void draw_time_setting_menu(GameState* state) {
    int x = (SCREEN_WIDTH - MENU_WIDTH) / 2;
    int y = (SCREEN_HEIGHT - MENU_HEIGHT) / 2;
    
    // Draw menu background
    Paint_DrawRectangle(x, y, x + MENU_WIDTH, y + MENU_HEIGHT, BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawRectangle(x + 1, y + 1, x + MENU_WIDTH - 1, y + MENU_HEIGHT - 1, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    
    // Draw title
    Paint_DrawString_EN(x + MENU_PADDING, y + MENU_PADDING, "SET TIME", &Font20, WHITE, BLACK);
    Paint_DrawLine(x + MENU_PADDING, y + MENU_PADDING + 25, x + MENU_WIDTH - MENU_PADDING, 
                  y + MENU_PADDING + 25, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    
    // Format time string
    char time_str[20];
    char am_pm_str[3] = "AM";
    if (state->time_settings.is_pm) {
        strcpy(am_pm_str, "PM");
    }
    
    // Draw time with appropriate highlighting
    int time_y = y + MENU_PADDING + 50;
    
    // Draw hour
    if (state->time_settings.editing == 0) {
        // Highlight hour if editing
        char hour_str[3];
        snprintf(hour_str, sizeof(hour_str), "%2d", 
                 state->time_settings.hour == 0 ? 12 : state->time_settings.hour);
        
        Paint_DrawRectangle(x + MENU_WIDTH/2 - 40, time_y - 2, 
                          x + MENU_WIDTH/2 - 15, time_y + 20, 
                          WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_EN(x + MENU_WIDTH/2 - 38, time_y, hour_str, &Font20, WHITE, BLACK);
    } else {
        char hour_str[3];
        snprintf(hour_str, sizeof(hour_str), "%2d", 
                 state->time_settings.hour == 0 ? 12 : state->time_settings.hour);
        Paint_DrawString_EN(x + MENU_WIDTH/2 - 38, time_y, hour_str, &Font20, WHITE, BLACK);
    }
    
    // Draw colon
    Paint_DrawString_EN(x + MENU_WIDTH/2 - 15, time_y, ":", &Font20, WHITE, BLACK);
    
    // Draw minute
    if (state->time_settings.editing == 1) {
        // Highlight minute if editing
        char minute_str[3];
        snprintf(minute_str, sizeof(minute_str), "%02d", state->time_settings.minute);
        
        Paint_DrawRectangle(x + MENU_WIDTH/2, time_y - 2, 
                          x + MENU_WIDTH/2 + 30, time_y + 20, 
                          WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_EN(x + MENU_WIDTH/2, time_y, minute_str, &Font20, WHITE, BLACK);
    } else {
        char minute_str[3];
        snprintf(minute_str, sizeof(minute_str), "%02d", state->time_settings.minute);
        Paint_DrawString_EN(x + MENU_WIDTH/2, time_y, minute_str, &Font20, WHITE, BLACK);
    }
    
    // Draw AM/PM
    if (state->time_settings.editing == 2) {
        // Highlight AM/PM if editing
        Paint_DrawRectangle(x + MENU_WIDTH/2 + 35, time_y - 2, 
                          x + MENU_WIDTH/2 + 65, time_y + 20, 
                          WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_EN(x + MENU_WIDTH/2 + 35, time_y, am_pm_str, &Font20, WHITE, BLACK);
    } else {
        Paint_DrawString_EN(x + MENU_WIDTH/2 + 35, time_y, am_pm_str, &Font20, WHITE, BLACK);
    }
    
    // Draw Save option
    if (state->time_settings.editing == 3) {
        Paint_DrawRectangle(x + MENU_WIDTH/2 - 30, time_y + 40, 
                          x + MENU_WIDTH/2 + 30, time_y + 65, 
                          WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_EN(x + MENU_WIDTH/2 - 25, time_y + 45, "SAVE", &Font16, WHITE, BLACK);
    } else {
        Paint_DrawRectangle(x + MENU_WIDTH/2 - 30, time_y + 40, 
                          x + MENU_WIDTH/2 + 30, time_y + 65, 
                         BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
        Paint_DrawString_EN(x + MENU_WIDTH/2 - 25, time_y + 45, "SAVE", &Font16, WHITE, BLACK);
    }
    
    // Draw navigation hint
    Paint_DrawString_EN(x + MENU_PADDING, y + MENU_HEIGHT - 25, 
                      "Scroll: Change  Select: Next", &Font8, WHITE, BLACK);
}
/* Modify the handle_buttons() function */
void handle_buttons() {
    static bool last_main_btn_state = HIGH;
    static bool last_scroll_btn_state = HIGH;
    unsigned long current_time = millis();
    
    // Handle main button (PIN_BUTTON) - for selection and double-clicks
    bool main_btn_state = digitalRead(PIN_BUTTON);
    
    if (main_btn_state == LOW && last_main_btn_state == HIGH && 
        current_time - state.last_button_press > BUTTON_DEBOUNCE_MS) {
        
        // If pet is frozen, resume animation
        if (state.pet_state == STATE_FROZEN && state.menu_state == MENU_STATE_NONE) {
            Serial.println("Resuming animation");
            state.pet_state = STATE_ANIMATING;
            state.animation_cycles = 0; // Reset cycle counter
            state.current_frame = 0;    // Start from first frame
            state.button_pressed = true;
            state.score++;
        }
        // Handle double-click for menu
        else if (current_time - state.last_button_press < DOUBLE_CLICK_TIME_MS && state.click_count > 0) {
            Serial.println("Double click detected!");
            state.click_count = 0;
            
            if (state.menu_state == MENU_STATE_NONE) {
                state.menu_state = MENU_STATE_MAIN;
                state.menu_selection = 0;
                state.pet_state = STATE_MENU;
                Serial.println("Opening menu");
            }
        } 
        // Handle regular button press
        else {
            state.click_count++;
            
            if (state.menu_state == MENU_STATE_MAIN) {
                handle_menu_actions();
            } 
            else if (state.menu_state == MENU_STATE_SET_TIME) {
                state.time_settings.editing = (state.time_settings.editing + 1) % 4;
                
                if (state.time_settings.editing == 0) {
                    //convert_time_settings_to_paint_time(&state.time_settings, &state.current_time);
                    state.menu_state = MENU_STATE_MAIN;
                }
            }
            else if (state.pet_state == STATE_ANIMATING) {
                state.score++;
                state.button_pressed = true;
                Serial.printf("Button pressed! Score: %d\n", state.score);
            }
        }
        state.last_button_press = current_time;
    }
    last_main_btn_state = main_btn_state;
    
    // Reset click count after a timeout
    if (state.click_count > 0 && current_time - state.last_button_press > DOUBLE_CLICK_TIME_MS) {
        state.click_count = 0;
    }
    
    // Handle scroll button (PIN_BUTTON_SCROLL)
    bool scroll_btn_state = digitalRead(PIN_BUTTON_SCROLL);
    
    if (scroll_btn_state == LOW && last_scroll_btn_state == HIGH && 
        current_time - state.last_scroll_press > BUTTON_DEBOUNCE_MS) {
        
        Serial.println("Scroll button pressed!");
        
        if (state.menu_state == MENU_STATE_MAIN) {
            state.menu_selection = (state.menu_selection + 1) % MENU_ITEM_COUNT;
        } 
        else if (state.menu_state == MENU_STATE_SET_TIME) {
            if (state.time_settings.editing == 0) {
                state.time_settings.hour = (state.time_settings.hour % 12) + 1;
            } 
            else if (state.time_settings.editing == 1) {
                state.time_settings.minute = (state.time_settings.minute + 1) % 60;
            } 
            else if (state.time_settings.editing == 2) {
                state.time_settings.is_pm = !state.time_settings.is_pm;
            }
        }
        
        state.last_scroll_press = current_time;
    }
    last_scroll_btn_state = scroll_btn_state;
}

// Fix cleanup function
void cleanup(void) {
    if (display_buffer) {
        free(display_buffer);
        display_buffer = NULL;
    }
    // Use DEV_Module_Init with 1 to exit
    DEV_Module_Init();
}

// Process menu selections
void handle_menu_actions() {
    switch (state.menu_selection) {
        case MENU_ITEM_RESET_SCORE:
            state.score = 0;
            Serial.println("Score reset to 0");
            break;
            
        case MENU_ITEM_RESET_BATTERY:
            state.battery = 100;
            Serial.println("Battery reset to 100%");
            break;
            
        case MENU_ITEM_SET_TIME:
            state.menu_state = MENU_STATE_SET_TIME;
            
            if (state.current_time.Hour >= 12) {
                state.time_settings.hour = state.current_time.Hour > 12 ? 
                                           state.current_time.Hour - 12 : 12;
                state.time_settings.is_pm = true;
            } else {
                state.time_settings.hour = state.current_time.Hour == 0 ? 12 : state.current_time.Hour;
                state.time_settings.is_pm = false;
            }
            
            state.time_settings.minute = state.current_time.Min;
            state.time_settings.editing = 0;
            break;
            
        case MENU_ITEM_EXIT:
            state.menu_state = MENU_STATE_NONE;
            // Return to frozen state after exiting menu
            state.pet_state = STATE_FROZEN;
            break;
    }
}

// Initialize hardware and resources
bool init_hardware(void) {
    if (DEV_Module_Init() != 0) {
        Serial.println("Hardware initialization failed!");
        return false;
    }

    // Configure button pins
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_BUTTON_SCROLL, INPUT_PULLUP);

    // Initialize display
    EPD_1IN54_V2_Init();
    EPD_1IN54_V2_Clear();
    
    // Allocate display buffer
    UWORD buffer_size = ((EPD_1IN54_V2_WIDTH % 8 == 0) ? 
                        (EPD_1IN54_V2_WIDTH / 8) : (EPD_1IN54_V2_WIDTH / 8 + 1)) * 
                        EPD_1IN54_V2_HEIGHT;
    
    display_buffer = (UBYTE*)malloc(buffer_size);
    if (!display_buffer) {
        Serial.println("Failed to allocate display buffer");
        return false;
    }

    // Initialize display buffer
    Paint_NewImage(display_buffer, EPD_1IN54_V2_WIDTH, EPD_1IN54_V2_HEIGHT, 0, WHITE);
    EPD_1IN54_V2_Init_Partial();
    
    return true;
}
/* Update the setup() function */
void setup() {
    // Initialize serial first for debugging
    Serial.begin(115200);
    Serial.println("ESP32 Virtual Pet Starting...");
    
    // Initialize state
    state = {
        .current_time = {12, 34, 56},
        .score = 0,
        .battery = 100,
        .button_pressed = false,
        .last_time_update = 0,
        .last_frame_time = 0,
        .current_frame = 0,
        .menu_state = MENU_STATE_NONE,
        .menu_selection = 0,
        .time_settings = {12, 0, false, 0},
        .last_button_press = 0,
        .last_scroll_press = 0,
        .click_count = 0,
        .pet_state = STATE_ANIMATING,
        .animation_cycles = 0
    };
    
    // Initialize hardware
    if (!init_hardware()) {
        Serial.println("Failed to initialize hardware!");
        while (1) { delay(1000); } // Halt if initialization fails
    }
    
    Serial.println("Initialization complete");
}

void loop() {
    unsigned long current_time = millis();
    
    // Handle button presses
    handle_buttons();

    // Update time (always, even when frozen)
    if (current_time - state.last_time_update >= UPDATE_INTERVAL) {
        update_time(&state.current_time);
        
        // Battery drain logic
        if (state.battery > 0 && (state.current_time.Sec % BATTERY_DRAIN == 0)) {
            state.battery--;
        }
        
        state.last_time_update = current_time;
    }

    // Update display
    if (current_time - state.last_frame_time >= FRAME_DELAY) {
        // Prepare display buffer
        Paint_SelectImage(display_buffer);
        Paint_Clear(WHITE);
        
        if (state.menu_state == MENU_STATE_NONE) {
            // Normal pet display
            const Animation* current_anim = state.button_pressed ? &button_anim : &normal_anim;
            
            // Draw current animation frame
            Paint_DrawBitMap(current_anim->frames[state.current_frame]);
            
            // Draw interface elements
            draw_interface(&state);
            
            // Update animation frame only if in animating state
            if (state.pet_state == STATE_ANIMATING) {
                // Move to next frame
                state.current_frame = (state.current_frame + 1) % current_anim->num_frames;
                
                // Count completed animation cycles
                if (state.current_frame == 0) {
                    state.animation_cycles++;
                    
                    // Reset button pressed state
                    if (state.button_pressed) {
                        state.button_pressed = false;
                    }
                    
                    // Check if we've completed the required cycles
                    if (state.animation_cycles >= ANIMATION_CYCLES && !state.button_pressed) {
                        state.pet_state = STATE_FROZEN;
                        // Stay on the last frame (0 is first frame, so go back one)
                        state.current_frame = current_anim->num_frames - 1;
                        Serial.println("Animation complete, freezing on last frame");
                    }
                }
            }
        } 
        else if (state.menu_state == MENU_STATE_MAIN) {
            // Draw main menu 
            draw_interface(&state);
            draw_menu(&state);
        }
        else if (state.menu_state == MENU_STATE_SET_TIME) {
            // Draw time setting menu
            draw_interface(&state);
            draw_time_setting_menu(&state);
        }
        
        // Update display
        EPD_1IN54_V2_DisplayPart(display_buffer);
        state.last_frame_time = current_time;
    }
}
