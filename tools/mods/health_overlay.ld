ENTRY(gui_hook)
SECTIONS {
 . = 0x603d270;
 .payload : { *(.text*) *(.rodata*) }
 /DISCARD/ : { *(.comment*) *(.eh_frame*) *(.note*) }
}
original_damage = 0x603dff0;
original_gui = 0x603dff8;
active_enemies = 0x2318c90;
game_time = 0x5b83c10;
main_camera = 0x5add370;
screen_width = 0x5af2d00;
screen_height = 0x5af2d60;
component_transform = 0x5b65e00;
transform_position = 0x5b8a060;
world_to_screen = 0x5adbee0;
white_texture = 0x5b390b0;
gui_color = 0x5bcfbc0;
gui_set_color = 0x5bcfd00;
gui_texture = 0x5bd2dc0;
gui_label = 0x5bd25d0;
gui_skin = 0x5bd1c90;
skin_label = 0x5bd2350;
style_font = 0x5beec60;
style_alignment = 0x5bedce0;
set_style_font = 0x5beeda0;
set_style_alignment = 0x5bede20;
string_ascii = 0x4d79ba0;
event_current = 0x5bcc870;
event_type = 0x5bcb2c0;
behaviour_active = 0x5b65160;
