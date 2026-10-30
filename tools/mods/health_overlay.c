/* Original native implementation for Switch; no Nexus code or assets included.
 * Supported binary only: 9049EA1C527A35D70F1ECE5E784C62E0.
 * Compiled freestanding: all engine calls use relative branches (ASLR safe).
 */
typedef unsigned char u8;
typedef struct { float x,y,z; } Vec3;
typedef struct { float x,y,w,h; } Rect;
typedef struct { float r,g,b,a; } Color;
extern void original_damage(void*,void*,void*);
extern void original_gui(void*,void*);
extern void* active_enemies(void*);
extern float game_time(void*);
extern void* main_camera(void*);
extern int screen_width(void*), screen_height(void*);
extern void* component_transform(void*,void*);
extern Vec3 transform_position(void*,void*);
extern Vec3 world_to_screen(void*,Vec3,void*);
extern void* white_texture(void*);
extern Color gui_color(void*);
extern void gui_set_color(Color,void*);
extern void gui_texture(Rect,void*,void*);
extern void gui_label(Rect,void*,void*,void*);
extern void* gui_skin(void*);
extern void* skin_label(void*,void*);
extern int style_font(void*,void*), style_alignment(void*,void*);
extern void set_style_font(void*,int,void*), set_style_alignment(void*,int,void*);
extern void* string_ascii(const char*,int,int,void*);
extern void* event_current(void*);
extern int event_type(void*,void*);
extern int behaviour_active(void*,void*);

#define AT(T,p,o) (*(T*)((u8*)(p)+(o)))
/* These two serialized legacy fields have no native accesses in this build.
 * They are not save data. Never port these offsets without repeating analysis.
 */
#define LAST_DAMAGE 0x1d0
#define LAST_TIME 0x1d4

void damage_hook(void* self, void* hit, void* method) {
    void* target=AT(void*,self,0xd8);
    if(!target) target=self;
    int before=AT(int,target,0xa8);
    int hero=AT(u8,hit,0xae);
    original_damage(self,hit,method);
    int after=AT(int,target,0xa8);
    if(hero && before>0 && before<=99999 && after<before && after>=-99999) {
        AT(int,target,LAST_DAMAGE)=0x48500000|(before-after);
        AT(float,target,LAST_TIME)=game_time(0);
    }
}

static void box(void* texture,float x,float y,float w,float h,Color color) {
    gui_set_color(color,0);
    gui_texture((Rect){x,y,w,h},texture,0);
}

static int digits(char* out,int n) {
    char rev[12]; int len=0, k=0;
    do { rev[len++]=(char)('0'+n%10); n/=10; } while(n>0);
    while(len) out[k++]=rev[--len];
    return k;
}

static void label(void* style,float x,float y,float w,float h,int current,int maximum) {
    char text[28]; int n=0;
    if(maximum==0) text[n++]='-';
    n+=digits(text+n,current);
    if(maximum>0) { text[n++]=' '; text[n++]='/'; text[n++]=' '; n+=digits(text+n,maximum); }
    text[n]=0;
    void* managed=string_ascii(text,0,n,0);
    gui_label((Rect){x,y,w,h},managed,style,0);
}

void gui_hook(void* self,void* method) {
    original_gui(self,method);
    void* event=event_current(0);
    if(!event || event_type(event,0)!=7) return; /* Repaint only. */
    void* list=active_enemies(0);
    if(!list) return;
    int count=AT(int,list,0x18);
    void* array=AT(void*,list,0x10);
    if(!array || count<=0 || count>4096) return;
    void* camera=main_camera(0);
    if(!camera) return;
    float width=(float)screen_width(0),height=(float)screen_height(0);
    float scale=height/720.f, now=game_time(0);
    Color saved=gui_color(0);
    void* skin=gui_skin(0);
    if(!skin) return;
    void* style=skin_label(skin,0);
    if(!style) return;
    int oldfont=style_font(style,0),oldalign=style_alignment(style,0);
    set_style_font(style,(int)(18.f*scale),0);
    set_style_alignment(style,4,0); /* MiddleCenter */
    void* texture=white_texture(0);
    int bosses=0,drawn=0;
    for(int i=0;i<count && drawn<24;i++) {
        void* hm=AT(void*,array,0x20+8*i);
        if(!hm || AT(u8,hm,0x1d9) || !behaviour_active(hm,0)) continue;
        unsigned int stamp=AT(unsigned int,hm,LAST_DAMAGE);
        int hp=AT(int,hm,0xa8),max=AT(int,hm,0x2ac),damage=(int)(stamp&0x3ffff);
        if((stamp&0xfffc0000)!=0x48500000 || !AT(u8,hm,0x2b0)) continue;
        if(hp<=0 || max<=0 || hp>99999 || max>99999 || damage<=0 || damage>199998) continue;
        float age=now-AT(float,hm,LAST_TIME);
        if(!(age>=0.f)) continue;
        int boss=max>105;
        if(!boss && age>3.f) continue;
        void* transform=component_transform(hm,0);
        if(!transform) continue;
        Vec3 world=transform_position(transform,0);
        world.y+=1.7f;
        Vec3 p=world_to_screen(camera,world,0);
        if(p.z<=0.f || p.x<0.f || p.x>width || p.y<0.f || p.y>height) continue;
        if(hp>max) max=hp;
        float bw=(boss?440.f:112.f)*scale,bh=(boss?16.f:9.f)*scale;
        float x=boss?(width-bw)*.5f:p.x-bw*.5f;
        float y=boss?height-(48.f+48.f*(float)bosses++)*scale:height-p.y;
        if(boss && bosses>3) continue;
        box(texture,x-2*scale,y-2*scale,bw+4*scale,bh+4*scale,(Color){.85f,.82f,.72f,.9f});
        box(texture,x,y,bw,bh,(Color){.06f,.06f,.08f,.92f});
        box(texture,x,y,bw*(float)hp/(float)max,bh,boss?(Color){.82f,.23f,.32f,1.f}:(Color){.83f,.8f,.7f,1.f});
        gui_set_color((Color){1,1,1,1},0);
        label(style,x-30*scale,y-25*scale,bw+60*scale,24*scale,hp,max);
        if(age<1.f) {
            gui_set_color((Color){1,.72f,.24f,1.f-age},0);
            label(style,p.x-80*scale,height-p.y-(40.f+22.f*age)*scale,160*scale,28*scale,damage,0);
        }
        drawn++;
    }
    set_style_font(style,oldfont,0);
    set_style_alignment(style,oldalign,0);
    gui_set_color(saved,0);
}
