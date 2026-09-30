/* ZeroSpades team ping presentation adapted to KyroSpades rendering.
 * Copyright (c) 2026 Fran6nd and ZeroSpades developers; GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "demo.h"
#include "camera.h"
#include "config.h"
#include "font.h"
#include "glx.h"
#include "matrix.h"
#include "player.h"
#include "teamplay.h"
#include "teamplay_draw.h"
#include "texture.h"

#define TP_PI 3.14159265358979323846f
static mat4 scene_view, scene_proj;
static bool scene_valid;
void teamplay_draw_capture(void) {
    matrix_load(scene_view, matrix_view);
    matrix_load(scene_proj, matrix_projection);
    scene_valid = true;
}
static float fade(const TeamplayPing* p) {
    return p->endless ? 1.f : fminf(1.f, fmaxf(0.f, p->remaining / .75f));
}
/* The ping body and its position stay fixed. Only the rim blinks: an
 * expanding/popping marker can make a precise callout look as though it is
 * moving, and a pulsing fill makes the location hard to read. */
static void diamond(float x, float y, float r, uint8_t red, uint8_t green, uint8_t blue, float alpha, float age) {
    if(alpha <= 0) return;
    float outer[18] = {x,y+r,0, x+r,y,0, x,y-r,0, x,y+r,0, x,y-r,0, x-r,y,0};
    const float inner_r = r*.78f;
    float body[18] = {x,y+inner_r,0, x+inner_r,y,0, x,y-inner_r,0,
                      x,y+inner_r,0, x,y-inner_r,0, x-inner_r,y,0};
    float core_r = r*.28f;
    float core[18] = {x,y+core_r,0, x+core_r,y,0, x,y-core_r,0,
                      x,y+core_r,0, x,y-core_r,0, x-core_r,y,0};
    glEnable(GL_BLEND);
    glColor4f(0, 0, 0, .8f * alpha);
    glx_draw_vertices_3d(outer, 6, GL_TRIANGLES);
    glColor4f(red/255.f, green/255.f, blue/255.f, alpha);
    glx_draw_vertices_3d(body, 6, GL_TRIANGLES);
    glColor4f(1, 1, 1, alpha * .75f);
    glx_draw_vertices_3d(core, 6, GL_TRIANGLES);

    /* Four fixed diamond edges, each a pair of triangles. Pulse their alpha,
       not the fill, radius, world position, or the text next to the marker. */
    float ox[4] = {x, x+r, x, x-r}, oy[4] = {y+r, y, y-r, y};
    float ix[4] = {x, x+inner_r, x, x-inner_r};
    float iy[4] = {y+inner_r, y, y-inner_r, y};
    float rim[72];
    for(int i=0; i<4; i++) {
        int n=(i+1)%4;
        float edge[18] = {ox[i],oy[i],0, ox[n],oy[n],0, ix[n],iy[n],0,
                          ox[i],oy[i],0, ix[n],iy[n],0, ix[i],iy[i],0};
        memcpy(rim+i*18,edge,sizeof(edge));
    }
    float blink = .5f + .5f*sinf(age*(2.f*TP_PI*1.25f));
    glColor4f(1,1,1,alpha*(.12f+.72f*blink));
    glx_draw_vertices_3d(rim,24,GL_TRIANGLES);
}
/* Use the sender's team colour (or our team's colour for server-owned
 * waypoints); keep the server colour if the team is unknown. */
static void ping_color(int id, const TeamplayPing* p, uint8_t* red, uint8_t* green, uint8_t* blue) {
    *red = p->red; *green = p->green; *blue = p->blue;
    int team = id != 255 && players[id].connected ? players[id].team : players[local_player_id].team;
    if(team == TEAM_1) {
        *red = gamestate.team_1.red; *green = gamestate.team_1.green; *blue = gamestate.team_1.blue;
    } else if(team == TEAM_2) {
        *red = gamestate.team_2.red; *green = gamestate.team_2.green; *blue = gamestate.team_2.blue;
    }
}

/* Same frame grid and 24-fps playback as the block-debris particles, but
 * screen-facing and larger so it stays legible at waypoint distances.
 * The half-texel inset prevents GL_LINEAR bleeding adjacent atlas frames. */
static void ping_sprite(int id, const TeamplayPing* p, float x, float y, float size, float alpha) {
    uint8_t red, green, blue;
    ping_color(id, p, &red, &green, &blue);
    struct texture* t = &texture_particle_anim;
    if(!t->texture_id || t->width <= 0 || t->height <= 0) {
        diamond(x, y, size * .3f, red, green, blue, alpha, p->age);
        return;
    }
    int cols = 8, rows = 8;
    if(t->width > t->height) { cols = (t->width + t->height / 2) / t->height; rows = 1; }
    else if(t->height > t->width) { cols = 1; rows = (t->height + t->width / 2) / t->width; }
    int frames = cols * rows;
    int frame = (int)fmodf(fmaxf(0.f, p->age) * 24.f, (float)frames);
    float cell_w = (float)t->width / cols, cell_h = (float)t->height / rows;
    if(cell_w <= 1.f || cell_h <= 1.f) {
        diamond(x, y, size * .3f, red, green, blue, alpha, p->age);
        return;
    }
    float u = (frame % cols * cell_w + .5f) / t->width;
    float v = (frame / cols * cell_h + .5f) / t->height;
    float us = (cell_w - 1.f) / t->width, vs = (cell_h - 1.f) / t->height;
    glColor4f(red / 255.f, green / 255.f, blue / 255.f, alpha);
    texture_draw_sector(t, x - size * .5f, y + size * .5f, size, size, u, v, us, vs);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor3f(1, 1, 1);
    glEnable(GL_BLEND); /* texture_draw_sector disables blend on legacy GL */
}

static void begin(void) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
static void end(void) { glColor3f(1,1,1); glDisable(GL_BLEND); }
static void label_color(float x, float y, const char* text, float alpha,
                        uint8_t red, uint8_t green, uint8_t blue) {
    if(!text || !*text) return;
    font_select(FONT_FIXEDSYS);
    glColor4f(red/255.f,green/255.f,blue/255.f,alpha);
    font_render_shadow(x - font_length(14, (char*)text)*.5f, y, 14, (char*)text, .8f*alpha);
}
static void label(float x, float y, const char* text, float alpha) {
    label_color(x, y, text, alpha, 255, 255, 255);
}
static bool project(float x,float y,float z, float* out_x,float* out_y,float* forward) {
    vec4 v = {x,y,z,1}, eye, clip;
    glmc_mat4_mulv(scene_view, v, eye);
    glmc_mat4_mulv(scene_proj, eye, clip);
    *forward = clip[3];
    if(!isfinite(clip[3]) || clip[3] < .001f) return false;
    *out_x = (clip[0] / clip[3] + 1.f) * .5f * settings.window_width;
    *out_y = (clip[1] / clip[3] + 1.f) * .5f * settings.window_height;
    return isfinite(*out_x) && isfinite(*out_y);
}
void teamplay_draw_world(void) {
    if(!scene_valid || (!network_connected && !demo_is_playing()) || network_map_transfer) return;
    begin();
    float sw = settings.window_width, sh = settings.window_height;
    float size = fminf(88.f, fmaxf(60.f, sh * .09f));
    float margin = size * .55f + 3.f;
    for(int id = 0; id < TEAMPLAY_PLAYERS; id++) {
        const TeamplayPing* p = &teamplay_pings[id];
        if(!p->active || !(p->surfaces & TEAMPLAY_WORLD)) continue;
        float x = p->x, y = 63.f - p->z, z = p->y, px = 0, py = 0, f = 0;
        bool front = project(x,y,z, &px,&py,&f);
        bool onscreen = front && px >= margin && px <= sw-margin && py >= margin && py <= sh-margin;
        if(!front) {
            /* Edge cue points in view space, including when the target is behind us. */
            vec4 v={x,y,z,1}, eye;
            glmc_mat4_mulv(scene_view,v,eye);
            float dx=eye[0], dy=eye[1];
            if(fabsf(dx)+fabsf(dy)<.001f) dy=1;
            float len=hypotf(dx,dy);
            px=sw*.5f+dx/len*sh*.5f; py=sh*.5f+dy/len*sh*.5f;
        }
        /* A small vertical bob makes the sprite float without altering the
           server-provided waypoint or the map's precise location. */
        py += sinf(p->age * (2.f * TP_PI * .8f)) * 4.f;
        px=fmaxf(margin,fminf(sw-margin,px)); py=fmaxf(margin,fminf(sh-margin,py));
        float alpha=fade(p);
        if(onscreen) {
            float d=hypotf(px-sw*.5f,py-sh*.5f);
            alpha*=fminf(1.f, fmaxf(.2f, (d-size*.5f)/size));
        }
        ping_sprite(id,p,px,py,size,alpha);
        char text[150];
        const char* name = (id != 255 && players[id].connected) ? players[id].name : "";
        snprintf(text,sizeof(text),"%s%s%s",name,*name && *p->reason ? ": " : "",p->reason);
        if(!onscreen) {
            size_t len=strlen(text);
            snprintf(text+len,sizeof(text)-len,"%s[%.0f]",len?" ":"",sqrtf((x-camera_x)*(x-camera_x)+(y-camera_y)*(y-camera_y)+(z-camera_z)*(z-camera_z)));
        }
        label(px,py-size*.55f-7.f,text,alpha);
    }
    /* Marks follow players, not a frozen packet position. SHOW_NAME is a
       server decision; reason alone may be shown without revealing identity. */
    for(int id = 0; id < TEAMPLAY_PLAYERS - 1; id++) {
        const TeamplayMark* m = &teamplay_marks[id];
        if(!m->active || !(m->surfaces & TEAMPLAY_WORLD) || id == local_player_id
           || !players[id].connected || !players[id].alive
           || players[id].team == TEAM_SPECTATOR) continue;
        float px, py, f;
        if(!project(players[id].physics.eye.x,
                    players[id].physics.eye.y + player_height(&players[id]) + .8f,
                    players[id].physics.eye.z, &px, &py, &f)
           || px < 0 || px > sw || py < 0 || py > sh) continue;
        char line[110];
        snprintf(line, sizeof(line), "%s%s%s", m->show_name ? players[id].name : "",
                 m->show_name && *m->reason ? ": " : "", m->reason);
        if(*line) label_color(px, py + 20.f, line, 1.f, m->red, m->green, m->blue);
    }
    if(teamplay_overlay_opacity() > 0.f && camera_mode == CAMERAMODE_FPS
       && players[local_player_id].team != TEAM_SPECTATOR) {
        for(int id = 0; id < TEAMPLAY_PLAYERS - 1; id++) {
            const struct Player* p = &players[id];
            if(id == local_player_id || !p->connected || !p->alive
               || p->team != players[local_player_id].team
               || (teamplay_marks[id].active && (teamplay_marks[id].surfaces & TEAMPLAY_WORLD)))
                continue;
            float px, py, f;
            if(project(p->physics.eye.x, p->physics.eye.y + player_height(p) + .8f,
                       p->physics.eye.z, &px, &py, &f)
               && px >= 0 && px <= sw && py >= 0 && py <= sh)
                label(px, py + 20.f, p->name, teamplay_overlay_opacity());
        }
    }
    /* Receiving a mark for ourselves is informative even when its only
       requested surface is the minimap (or a surface this client lacks). */
    if(local_player_id < TEAMPLAY_PLAYERS - 1 && teamplay_marks[local_player_id].active
       && players[local_player_id].connected) {
        const TeamplayMark* m = &teamplay_marks[local_player_id];
        char line[100];
        snprintf(line, sizeof(line), "YOU ARE MARKED%s%s", *m->reason ? ": " : "", m->reason);
        label_color(sw * .5f, sh * .84f, line, 1.f, m->red, m->green, m->blue);
    }
    end();
}
void teamplay_draw_map(float left,float top,float width,float height,float origin_x,float origin_y,float extent) {
    if(extent <= 0 || width <= 0 || height <= 0) return;
    begin();
    for(int id=0; id<TEAMPLAY_PLAYERS; id++) {
        const TeamplayPing* p=&teamplay_pings[id];
        if(p->active && (p->surfaces & TEAMPLAY_MAP)) {
            float x=left+(p->x-origin_x)/extent*width, y=top-(p->y-origin_y)/extent*height;
            float size = fminf(26.f, fmaxf(18.f, height * .04f));
            if(x>=left+size*.5f && x<=left+width-size*.5f
               && y<=top-size*.5f && y>=top-height+size*.5f)
                ping_sprite(id,p,x,y,size,fade(p));
        }
    }
    for(int id=0; id<TEAMPLAY_PLAYERS-1; id++) {
        const TeamplayMark* m=&teamplay_marks[id];
        if(!m->active || !(m->surfaces & TEAMPLAY_MAP) || !players[id].connected
           || !players[id].alive || players[id].team==TEAM_SPECTATOR) continue;
        float x=left+(players[id].pos.x-origin_x)/extent*width;
        float y=top-(players[id].pos.z-origin_y)/extent*height;
        float r=fminf(6.f,fmaxf(3.f,height*.01f));
        if(x<left+r || x>left+width-r || y>top-r || y<top-height+r) continue;
        glEnable(GL_BLEND);
        glColor4f(0,0,0,.85f);
        glx_draw_ring_segment_2d(x,y,0,r+1.f,0,2.f*TP_PI,24);
        glColor4f(m->red/255.f,m->green/255.f,m->blue/255.f,1.f);
        glx_draw_ring_segment_2d(x,y,0,r,0,2.f*TP_PI,24);
    }
    end();
}
