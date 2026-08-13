#ifndef __DIALPLATE_VIEW_H
#define __DIALPLATE_VIEW_H

#include "../Page.h"

namespace Page
{

class DialplateView
{

public:
    typedef struct
    {
        lv_obj_t* cont;
        lv_obj_t* lableValue;
        lv_obj_t* lableUnit;
    } SubInfo_t;

public:
    struct
    {
        struct
        {
            lv_obj_t* cont;
            lv_obj_t* labelSpeed;
            lv_obj_t* labelUint;
        } topInfo;

        struct
        {
            lv_obj_t* cont;
            SubInfo_t labelInfoGrp[3];
        } bottomInfo;

        struct
        {
            lv_obj_t* cont;
            lv_obj_t* dialBg;
            lv_obj_t* ring;
            lv_obj_t* contN;
            lv_obj_t* labelN;
            lv_obj_t* labelS;
            lv_obj_t* labelE;
            lv_obj_t* labelW;
            lv_obj_t* labelAngle;
            lv_obj_t* labelDir;
        } compass;

        struct
        {
            lv_obj_t* cont;
            lv_obj_t* btnMap;
            lv_obj_t* btnRec;
            lv_obj_t* btnMenu;
        } btnCont;

        struct
        {
            lv_obj_t* cont;
            lv_obj_t* labelValue;
        } brightness;

        lv_anim_timeline_t* anim_timeline;
    } ui;

    void Create(lv_obj_t* root);
    void Delete();
    void AppearAnimStart(bool reverse = false);

    void ShowBrightnessOverlay(bool show);
    void SetBrightnessValue(int32_t value);
    void SetCompassCourse(float course);

private:
    float compassCourse;

private:
    void TopInfo_Create(lv_obj_t* par);
    void BottomInfo_Create(lv_obj_t* par);
    void SubInfoGrp_Create(lv_obj_t* par, SubInfo_t* info, const char* unitText);
    void Compass_Create(lv_obj_t* par);
    void UpdateCompassPositions(float course);
    static void onCompassDraw(lv_event_t* event);
    void BtnCont_Create(lv_obj_t* par);
    void Brightness_Create(lv_obj_t* par);
    lv_obj_t* Btn_Create(lv_obj_t* par, const void* img_src, lv_coord_t x_ofs);
};

}

#endif // !__VIEW_H
