#include "oven.h"

profile_t* pxProfile;

static pid_struct_t *pOvenPID = NULL;

float fOvenTemp, fOvenTargetTemp = 0;

ovenMode_t xOvenMode = 4;

ovenErr_t xOvenErr;

void _wtimer0_isr()
{
    uint32_t ulFlags = WTIMER0->IFC;

    if(ulFlags & WTIMER_IF_OF)
    {
        WTIMER1->CC[1].CCV = (PHASE_ANGLE_WIDTH - CLIP(g_usPacLookup[(uint16_t)pOvenPID->fOutput], MIN_PHASE_ANGLE, MAX_PHASE_ANGLE)) / 0.028f;
        WTIMER1->CC[2].CCV = WTIMER1->CC[1].CCV + ((float)SSR_LATCH_OFFSET / 0.028f);
    }
}

void oven_init()
{
    pOvenPID = pid_init(PHASE_ANGLE_WIDTH, 0, PID_OPERATING_RANGE, PID_KI_CAP, PID_KP, PID_KI, PID_KD);

    if(pOvenPID)
        DBGPRINTLN_CTX("Oven PID init OK!");
    else
        DBGPRINTLN_CTX("Oven PID init NOK!");

    /*
        Function description:

        ZEROCROSS = Rising edge of the zero cross detector (goes high when the wave is about to cross, not after)
        WTIMER0_OF = Goes high ZEROCROSS_DELAY microseconds after ZEROCROSS
        PRS_CH0 = Generates a pulse when WTIMER0_OF goes high
        WTIMER1_CC1 = Goes low ZEROCROSS_DELAY microseconds after ZEROCROSS, goes high when the TRIAC should be turned on
        WTIMER1_CC2 = Goes low ZEROCROSS_DELAY microseconds after ZEROCROSS, goes high when the TRIAC should be turned off
        PRS_CH1 = Follows WTIMER1_CC1 and ANDs it with PRS_CH2
        PRS_CH2 = Follows WTIMER1_CC2 and inverts it
        TRIAC_OUT = TRIAC control signal

        --------------------------

        ZEROCROSS -> Trigger WTIMER0_CC0 -> Start WTIMER0
        WTIMER0_OF -> Pulse PRS_CH0 -> Trigger WTIMER1_CC0 -> Start WTIMER1
        WTIMER1_CC1 -> Level PRS_CH1 -------->
                                            -> AND -> TRIAC_OUT
        WTIMER1_CC2 -> Level PRS_CH2 -> NOT ->
    */

    // Wide Timer 0 - Delay zero cross
    CMU->HFPERCLKEN1 |= CMU_HFPERCLKEN1_WTIMER0;

    WTIMER0->CTRL = WTIMER_CTRL_RSSCOIST | WTIMER_CTRL_PRESC_DIV1 | WTIMER_CTRL_CLKSEL_PRESCHFPERCLK | WTIMER_CTRL_FALLA_NONE | WTIMER_CTRL_RISEA_RELOADSTART | WTIMER_CTRL_OSMEN | WTIMER_CTRL_MODE_UP;
    WTIMER0->TOP = (float)ZEROCROSS_DELAY / 0.028f;
    WTIMER0->CNT = 0x00000000;
    WTIMER0->ROUTELOC0 = ((uint32_t)0 << _WTIMER_ROUTELOC0_CC0LOC_SHIFT);
    WTIMER0->ROUTEPEN |= WTIMER_ROUTEPEN_CC0PEN;

    WTIMER0->CC[0].CTRL = WTIMER_CC_CTRL_FILT_ENABLE | WTIMER_CC_CTRL_INSEL_PIN | WTIMER_CC_CTRL_CUFOA_NONE | WTIMER_CC_CTRL_COFOA_NONE | WTIMER_CC_CTRL_CMOA_NONE | WTIMER_CC_CTRL_MODE_OFF;

    WTIMER0->IFC = _WTIMER_IFC_MASK; // Clear all flags
    IRQ_CLEAR(WTIMER0_IRQn); // Clear pending vector
    IRQ_SET_PRIO(WTIMER0_IRQn, 0, 0); // Set priority 0,0 (max)
    IRQ_ENABLE(WTIMER0_IRQn); // Enable vector
    WTIMER0->IEN = WTIMER_IEN_OF; // Enable OF flag

    // Wide Timer 1 - Output phase angle control
    CMU->HFPERCLKEN1 |= CMU_HFPERCLKEN1_WTIMER1;

    WTIMER1->CTRL = WTIMER_CTRL_RSSCOIST | WTIMER_CTRL_PRESC_DIV1 | WTIMER_CTRL_CLKSEL_PRESCHFPERCLK | WTIMER_CTRL_FALLA_NONE | WTIMER_CTRL_RISEA_RELOADSTART | WTIMER_CTRL_OSMEN | WTIMER_CTRL_MODE_UP;
    WTIMER1->TOP = 0xFFFFFFFF;
    WTIMER1->CNT = 0x00000000;

    WTIMER1->CC[0].CTRL = WTIMER_CC_CTRL_INSEL_PRS | WTIMER_CC_CTRL_PRSSEL_PRSCH0 | WTIMER_CC_CTRL_CUFOA_NONE | WTIMER_CC_CTRL_COFOA_NONE | WTIMER_CC_CTRL_CMOA_NONE | WTIMER_CC_CTRL_MODE_OFF;

    WTIMER1->CC[1].CTRL = WTIMER_CC_CTRL_PRSCONF_LEVEL | WTIMER_CC_CTRL_CUFOA_NONE | WTIMER_CC_CTRL_COFOA_NONE | WTIMER_CC_CTRL_CMOA_SET | WTIMER_CC_CTRL_MODE_OUTPUTCOMPARE;
    WTIMER1->CC[1].CCV = (float)7500 / 0.028f;

    WTIMER1->CC[2].CTRL = WTIMER_CC_CTRL_PRSCONF_LEVEL | WTIMER_CC_CTRL_CUFOA_NONE | WTIMER_CC_CTRL_COFOA_NONE | WTIMER_CC_CTRL_CMOA_SET | WTIMER_CC_CTRL_MODE_OUTPUTCOMPARE;
    WTIMER1->CC[2].CCV = (float)(7500 + SSR_LATCH_OFFSET) / 0.028f;

    // PRS
    CMU->HFBUSCLKEN0 |= CMU_HFBUSCLKEN0_PRS;

    PRS->CH[0].CTRL = PRS_CH_CTRL_SOURCESEL_WTIMER0 | PRS_CH_CTRL_SIGSEL_WTIMER0OF;

    PRS->CH[11].CTRL = PRS_CH_CTRL_ANDNEXT | PRS_CH_CTRL_SOURCESEL_WTIMER1 | PRS_CH_CTRL_SIGSEL_WTIMER1CC1;
    PRS->CH[12].CTRL = PRS_CH_CTRL_INV | PRS_CH_CTRL_SOURCESEL_WTIMER1 | PRS_CH_CTRL_SIGSEL_WTIMER1CC2;

    PRS->ROUTELOC2 |= ((uint32_t)2 << _PRS_ROUTELOC2_CH11LOC_SHIFT); // Output for the SSR
    PRS->ROUTEPEN |= PRS_ROUTEPEN_CH11PEN;

    //PRS->ROUTELOC0 |= PRS_ROUTELOC0_CH0LOC_LOC2; // Output Zero Cross for debug purposes
    //PRS->ROUTEPEN |= PRS_ROUTEPEN_CH0PEN;

    pxProfile = profile_new();

    profile_node_t* preheat_node = profile_rpush(pxProfile, profile_node_new(160, 60000));
    profile_node_t* soak_node = profile_rpush(pxProfile, profile_node_new(190, 60000));
    profile_node_t* reflow_node = profile_rpush(pxProfile, profile_node_new(230, 60000));
    profile_node_t* cooldown_node = profile_rpush(pxProfile, profile_node_new(60, 60000));
}

void oven_task()
{
    static uint64_t ullLastTempCheck = 0;
    static uint64_t ullLastStateUpdate = 0;
    static uint64_t ullLastPIDUpdate = 0;

    if(g_ullSystemTick > (ullLastTempCheck + 100))
    {
        uint8_t ubStatus = mcp9600_get_status(0);

        if(ubStatus & MCP9600_TH_UPDT)
        {
            fOvenTemp = mcp9600_get_hj_temp(0);
            //float fCold = mcp9600_get_cj_temp(MCP9600_0);
            //float fDelta = mcp9600_get_temp_delta(MCP9600_0);

            mcp9600_set_status(0, 0x00);
            //mcp9600_set_config(MCP9600_BURST_TS_1 | MCP9600_MODE_NORMAL);

            pOvenPID->fDeltaTime = (float)(g_ullSystemTick - ullLastPIDUpdate) * 0.001f;
            pOvenPID->fValue = fOvenTemp;
            pOvenPID->fSetpoint = fOvenTargetTemp;

            pid_calc(pOvenPID);

            //DBGPRINTLN_CTX("PID - Last update: %llu ms ago", g_ullSystemTick - ullLastPIDUpdate);
            //DBGPRINTLN_CTX("PID - MCP9600 temp %.3f C", fOvenTemp);
            //DBGPRINTLN_CTX("PID - MCP9600 cold %.3f C", fCold);
            //DBGPRINTLN_CTX("PID - MCP9600 delta %.3f C", fDelta);
            //DBGPRINTLN_CTX("PID - temp target %.3f C", pOvenPID->fSetpoint);
            //DBGPRINTLN_CTX("PID - integral %.3f", pOvenPID->fIntegral);
            //DBGPRINTLN_CTX("PID - output %.2f / %d", pOvenPID->fOutput, PHASE_ANGLE_WIDTH);
            //DBGPRINTLN_CTX("PID - output linear compensated %d / %d", g_usPacLookup[(uint16_t)pOvenPID->fOutput], PHASE_ANGLE_WIDTH);

            ullLastPIDUpdate = g_ullSystemTick;
        }

        ullLastTempCheck = g_ullSystemTick;
    }

    if(g_ullSystemTick > (ullLastStateUpdate + 500))
    {
        static uint64_t ullTimer = 0;

        static profile_node_t *node = NULL;
        static profile_iterator_t *it = NULL;

        static ovenMode_t lastMode = ABORT;

        static uint64_t nodeStart;

        switch(xOvenMode)
        {

            case REFLOW:     // reflow
                if(lastMode != REFLOW)
                {
                    if(it) profile_iterator_destroy(it);
                    it = profile_iterator_new(pxProfile, PROFILE_HEAD);
                    node = profile_iterator_next(it);
                    nodeStart = g_ullSystemTick;
                    fOvenTargetTemp = node->target;
                }
                if(g_ullSystemTick > (node->minTime + nodeStart))
                {
                    if(node = profile_iterator_next(it))
                    {
                        nodeStart = g_ullSystemTick;
                        fOvenTargetTemp = node->target;
                    }
                    else
                    {
                        fOvenTargetTemp = 0;
                        xOvenMode = IDLE;
                    }
                }
                break;

            case ABORT:
                break;

            case IDLE:     // idle
                //DBGPRINTLN_CTX("State - idle");
                fOvenTargetTemp = 0;
                break;

            default:
                oven_abort(ERRONEOUS_STATE);
                fOvenTargetTemp = 0;
                break;
        }

        lastMode = xOvenMode;
        ullLastStateUpdate = g_ullSystemTick;
    }


}

ovenMode_t oven_get_mode()
{
    return xOvenMode;
}

float oven_get_temperature()
{
    return fOvenTemp;
}

float oven_get_target_temperature()
{
    return fOvenTargetTemp;
}

void oven_start()
{
    xOvenMode = REFLOW;
}

void oven_abort(ovenErr_t reason)
{
    xOvenErr = reason;
    xOvenMode = ABORT;
}

ovenErr_t oven_get_err()
{
    return xOvenErr;
}

void oven_clr_err()
{
    xOvenMode = IDLE;
}