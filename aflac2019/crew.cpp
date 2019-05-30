//
//  crew.cpp
//  cppTest
//
//  Created by Wataru Taniguchi on 2019/04/28.
//  Copyright © 2019 Wataru Taniguchi. All rights reserved.
//

#include "app.h"
#include "balancer.h"

#include "crew.hpp"

/* Observer event flags */
/* ToDo: replace these by priorityQueue */
bool bt_flag         = false; /* Bluetoothコマンド true:リモートスタート */
bool touch_flag      = false; /* TouchSensor true:タッチセンサー押下 */
bool sonar_flag      = false; /* SonarSensor true:障害物検知 */
bool backButton_flag = false; /* true: BackButton押下 */

extern Observer*    observer;
extern Navigator*   activeNavigator;
extern Clock*       clock;
extern bool         landing;

Observer::Observer(TouchSensor* ts,SonarSensor* ss) {
    syslog(LOG_NOTICE, "Observer constructor");
    touchSensor = ts;
    sonarSensor = ss;
    bt = NULL;
}

void Observer::goOnDuty() {
    /* Open Bluetooth file */
    //bt = ev3_serial_open_file(EV3_SERIAL_BT);
    //assert(bt != NULL);

    // register cyclic handler to EV3RT
    ev3_sta_cyc(CYC_OBS_TSK);
    syslog(LOG_NOTICE, "Observer handler set");
}

void Observer::operate() {
    //if (check_bt())         bt_flag         = true;
    if (check_touch()) {
        syslog(LOG_NOTICE, "TouchSensor set off");
        touch_flag      = true;
    }
    if (check_sonar())      sonar_flag      = true;
    if (check_backButton()) backButton_flag = true;
}

void Observer::goOffDuty() {
    // deregister cyclic handler from EV3RT
    ev3_stp_cyc(CYC_OBS_TSK);
    syslog(LOG_NOTICE, "Observer handler unset");
    
    //fclose(bt);
}

bool Observer::check_touch(void) {
    if (touchSensor->isPressed()) {
        return true;
    } else {
        return false;
    }
}

bool Observer::check_sonar(void) {
    int32_t distance = sonarSensor->getDistance();
    if ((distance <= SONAR_ALERT_DISTANCE) && (distance >= 0)) {
        return true; // obstacle detected - alert
    } else {
        return false; // no problem
    }
}

bool Observer::check_bt(void) {
    uint8_t c = fgetc(bt); /* 受信 */
    fputc(c, bt); /* エコーバック */
    switch(c)
    {
        case '1':
            return true;
        default:
            return false;
    }
}

bool Observer::check_backButton(void) {
    if (ev3_button_is_pressed(BACK_BUTTON)) {
        return true;
    } else {
        return false;
    }
}

Observer::~Observer() {
    syslog(LOG_NOTICE, "Observer destructor");
}

Navigator::Navigator() {
    syslog(LOG_NOTICE, "Navigator default constructor");
}

//*****************************************************************************
// 引数 : lpwm (左モーターPWM値 ※前回の出力値)
//        rpwm (右モーターPWM値 ※前回の出力値)
//        lenc (左モーターエンコーダー値)
//        renc (右モーターエンコーダー値)
// 返り値 : なし
// 概要 : 直近のPWM値に応じてエンコーダー値にバックラッシュ分の値を追加します。
//*****************************************************************************
void Navigator::cancelBacklash(int8_t lpwm, int8_t rpwm, int32_t *lenc, int32_t *renc) {
    const int32_t BACKLASHHALF = 4;   // バックラッシュの半分[deg]
    
    if(lpwm < 0) *lenc += BACKLASHHALF;
    else if(lpwm > 0) *lenc -= BACKLASHHALF;
    
    if(rpwm < 0) *renc += BACKLASHHALF;
    else if(rpwm > 0) *renc -= BACKLASHHALF;
}

//*****************************************************************************
// 引数 : angle (モータ目標角度[度])
// 返り値 : 無し
// 概要 : 走行体完全停止用モータの角度制御
//*****************************************************************************
void Navigator::controlTail(int32_t angle) {
    float pwm = (float)(angle - tailMotor->getCount()) * P_GAIN; /* 比例制御 */
    /* PWM出力飽和処理 */
    if (pwm > PWM_ABS_MAX) {
        pwm = PWM_ABS_MAX;
    } else if (pwm < -PWM_ABS_MAX) {
        pwm = -PWM_ABS_MAX;
    }
    tailMotor->setPWM(pwm);
}

Navigator::~Navigator() {
    syslog(LOG_NOTICE, "Navigator destructor");
}

AnchorWatch::AnchorWatch(Motor* tm) {
    syslog(LOG_NOTICE, "AnchorWatch constructor");
    tailMotor   = tm;
}

void AnchorWatch::goOnDuty() {
    syslog(LOG_NOTICE, "AnchorWatch goes on duty");

    /* 尻尾モーターのリセット */
    tailMotor->reset();
    ev3_led_set_color(LED_ORANGE); /* 初期化完了通知 */

    if (activeNavigator == NULL) {
        activeNavigator = this;
        // register cyclic handler to EV3RT
        ev3_sta_cyc(CYC_NAV_TSK);
        syslog(LOG_NOTICE, "Navigator handler set");
    }
}

void AnchorWatch::operate() {
    //syslog(LOG_NOTICE, "AnchorWatch is in action...");
    controlTail(TAIL_ANGLE_STAND_UP); /* 完全停止用角度に制御 */
}

void AnchorWatch::goOffDuty() {
    syslog(LOG_NOTICE, "AnchorWatch goes off duty");
    if (activeNavigator == this) {
        // deregister cyclic handler from EV3RT
        ev3_stp_cyc(CYC_NAV_TSK);
        syslog(LOG_NOTICE, "Navigator handler unset");
        clock->sleep(1000); // wait a while
        activeNavigator = NULL;
    }
    
    /* 走行モーターエンコーダーリセット */
    leftMotor->reset();
    rightMotor->reset();
    
    /* ジャイロセンサーリセット */
    gyroSensor->reset();
    balance_init(); /* 倒立振子API初期化 */
    
    ev3_led_set_color(LED_GREEN); /* スタート通知 */
}

AnchorWatch::~AnchorWatch() {
    syslog(LOG_NOTICE, "AnchorWatch destructor");
}

LineTracer::LineTracer(Motor* lm, Motor* rm, Motor* tm, GyroSensor* gs, ColorSensor* cs) {
    syslog(LOG_NOTICE, "LineTracer constructor");
    leftMotor   = lm;
    rightMotor  = rm;
    tailMotor   = tm;
    gyroSensor  = gs;
    colorSensor = cs;
}

void LineTracer::goOnDuty() {
    syslog(LOG_NOTICE, "LineTracer goes on duty");
    if (activeNavigator == NULL) {
        activeNavigator = this;
        // register cyclic handler to EV3RT
        ev3_sta_cyc(CYC_NAV_TSK);
        syslog(LOG_NOTICE, "Navigator handler set");
    }
}

void LineTracer::operate() {
    controlTail(TAIL_ANGLE_DRIVE); /* バランス走行用角度に制御 */
    
    if (sonar_flag) {
        forward = turn = 0; /* 障害物を検知したら停止 */
        sonar_flag = false;
    } else {
        //forward = 30; /* 前進命令 */
        forward = 5;
        if (colorSensor->getBrightness() >= (LIGHT_WHITE + LIGHT_BLACK)/2) {
            turn =  20; /* 左旋回命令 */
        } else {
            turn = -20; /* 右旋回命令 */
        }
    }
    /* 倒立振子制御API に渡すパラメータを取得する */
    motor_ang_l = leftMotor->getCount();
    motor_ang_r = rightMotor->getCount();
    gyro = gyroSensor->getAnglerVelocity();
    volt = ev3_battery_voltage_mV();
    
    /* バックラッシュキャンセル */
    cancelBacklash(pwm_L, pwm_R, &motor_ang_l, &motor_ang_r);
    
    /* 倒立振子制御APIを呼び出し、倒立走行するための */
    /* 左右モータ出力値を得る */
    balance_control((float)forward,
                    (float)turn,
                    (float)gyro,
                    (float)GYRO_OFFSET,
                    (float)motor_ang_l,
                    (float)motor_ang_r,
                    (float)volt,
                    (int8_t *)&pwm_L,
                    (int8_t *)&pwm_R);
    
    syslog(LOG_NOTICE, "pwm_L = %d, pwm_R = %d", pwm_L, pwm_R);
    leftMotor->setPWM(pwm_L);
    rightMotor->setPWM(pwm_R);
}

void LineTracer::goOffDuty() {
    syslog(LOG_NOTICE, "LineTracer goes off duty");
    if (activeNavigator == this) {
        // deregister cyclic handler from EV3RT
        ev3_stp_cyc(CYC_NAV_TSK);
        syslog(LOG_NOTICE, "Navigator handler unset");
        clock->sleep(1000); // wait a while
        activeNavigator = NULL;
    }
}

LineTracer::~LineTracer() {
    syslog(LOG_NOTICE, "LineTracer destructor");
}

Captain::Captain() {
    syslog(LOG_NOTICE, "Captain default constructor");
}

void Captain::takeoff() {
    /* 各オブジェクトを生成・初期化する */
    touchSensor = new TouchSensor(PORT_1);
    colorSensor = new ColorSensor(PORT_3);
    sonarSensor = new SonarSensor(PORT_2);
    gyroSensor  = new GyroSensor(PORT_4);
    leftMotor   = new Motor(PORT_C);
    rightMotor  = new Motor(PORT_B);
    tailMotor   = new Motor(PORT_A);
    
    /* LCD画面表示 */
    ev3_lcd_fill_rect(0, 0, EV3_LCD_WIDTH, EV3_LCD_HEIGHT, EV3_LCD_WHITE);
    ev3_lcd_draw_string("EV3way-ET aflac2019", 0, CALIB_FONT_HEIGHT*1);
    
    // register cyclic handler to EV3RT
    ev3_sta_cyc(CYC_CAP_TSK);
    
    observer = new Observer(touchSensor, sonarSensor);
    observer->goOnDuty();
    //limboDancer = new LimboDancer(leftMotor, rightMotor, tailMotor);
    //seesawCrimber = new SeesawCrimber(leftMotor, rightMotor, tailMotor);
    lineTracer = new LineTracer(leftMotor, rightMotor, tailMotor, gyroSensor, colorSensor);
    anchorWatch = new AnchorWatch(tailMotor);
    anchorWatch->goOnDuty();
}

void Captain::operate() {
    //syslog(LOG_NOTICE, "Captain is in action...");
    
    /* ToDo: implement a state machine to pick up an appropriate Navigator */
    if (bt_flag || touch_flag) {
        syslog(LOG_NOTICE, "Departing...");
        activeNavigator->goOffDuty();
        //lineTracer->goOnDuty();
    }
    if (backButton_flag) {
        landing = true; // This will cause the main task to initiate the landing sequence
    }
    /*
    switch (event) {
     case "a":
        activeNavigator->goOffDuty();
        seesawCrimber->goOnDuty();
        break;
     case "b":
        activeNavigator->goOffDuty();
        lineTracer->goOnDuty();
        break;
     default:
        break;
    }
    */
}

void Captain::land() {
    if (activeNavigator != NULL) {
        activeNavigator->goOffDuty();
        clock->sleep(1000); // wait a while
    }
    leftMotor->reset();
    rightMotor->reset();
    
    delete anchorWatch;
    delete lineTracer;
    //delete seesawCrimber
    //delete limboDancer
    observer->goOffDuty();
    clock->sleep(1000); // wait a while
    delete observer;
    // deregister cyclic handler from EV3RT
    ev3_stp_cyc(CYC_CAP_TSK);
}

Captain::~Captain() {
    syslog(LOG_NOTICE, "Captain destructor");
}