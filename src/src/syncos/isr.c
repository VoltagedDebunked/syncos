#include <syncos/isr.h>
#include <kstd/stdio.h>
#include <kstd/asm.h>

// Debugging macro to output information without potential recursion
#define EMERGENCY_PRINT(str) \
    do { \
        const char *msg = str; \
        for (int i = 0; msg[i]; i++) { \
            __asm__ volatile ("outb %%al, $0x3F8" : : "a"(msg[i])); \
        } \
        __asm__ volatile ("outb %%al, $0x3F8" : : "a"('\n')); \
    } while(0)

// Common interrupt handler for all exceptions
void isr_common_handler(interrupt_frame_t *frame) {
    // More detailed exception handling
    printf("\n!!! EXCEPTION OCCURRED !!!\n");
    printf("Exception Vector: %lu\n", frame->int_no);
    printf("Error Code: %lu\n", frame->err_code);
    printf("Instruction Pointer: 0x%lx\n", frame->rip);
    printf("Code Segment: 0x%lx\n", frame->cs);
    printf("Flags: 0x%lx\n", frame->rflags);
    
    // Specific handling for divide by zero
    if (frame->int_no == 0) {
        printf("DIVIDE BY ZERO EXCEPTION DETECTED!\n");
    }
    
    // Halt the system
    while(1) {
        __asm__ volatile ("cli; hlt");
    }
}

// External declarations of ISR stub functions
extern void isr_0(void);
extern void isr_1(void);
extern void isr_2(void);
extern void isr_3(void);
extern void isr_4(void);
extern void isr_5(void);
extern void isr_6(void);
extern void isr_7(void);
extern void isr_8(void);
extern void isr_9(void);
extern void isr_10(void);
extern void isr_11(void);
extern void isr_12(void);
extern void isr_13(void);
extern void isr_14(void);
extern void isr_15(void);
extern void isr_16(void);
extern void isr_17(void);
extern void isr_18(void);
extern void isr_19(void);
extern void isr_20(void);
extern void isr_21(void);
extern void isr_22(void);
extern void isr_23(void);
extern void isr_24(void);
extern void isr_25(void);
extern void isr_26(void);
extern void isr_27(void);
extern void isr_28(void);
extern void isr_29(void);
extern void isr_30(void);
extern void isr_31(void);

// Generate stubs for additional vectors
#define GENERATE_ADDITIONAL_STUB(x) extern void isr_##x(void);
#define REPEAT_32_255(macro) \
    macro(32) macro(33) macro(34) macro(35) macro(36) macro(37) macro(38) macro(39) \
    macro(40) macro(41) macro(42) macro(43) macro(44) macro(45) macro(46) macro(47) \
    macro(48) macro(49) macro(50) macro(51) macro(52) macro(53) macro(54) macro(55) \
    macro(56) macro(57) macro(58) macro(59) macro(60) macro(61) macro(62) macro(63) \
    macro(64) macro(65) macro(66) macro(67) macro(68) macro(69) macro(70) macro(71) \
    macro(72) macro(73) macro(74) macro(75) macro(76) macro(77) macro(78) macro(79) \
    macro(80) macro(81) macro(82) macro(83) macro(84) macro(85) macro(86) macro(87) \
    macro(88) macro(89) macro(90) macro(91) macro(92) macro(93) macro(94) macro(95) \
    macro(96) macro(97) macro(98) macro(99) macro(100) macro(101) macro(102) macro(103) \
    macro(104) macro(105) macro(106) macro(107) macro(108) macro(109) macro(110) macro(111) \
    macro(112) macro(113) macro(114) macro(115) macro(116) macro(117) macro(118) macro(119) \
    macro(120) macro(121) macro(122) macro(123) macro(124) macro(125) macro(126) macro(127) \
    macro(128) macro(129) macro(130) macro(131) macro(132) macro(133) macro(134) macro(135) \
    macro(136) macro(137) macro(138) macro(139) macro(140) macro(141) macro(142) macro(143) \
    macro(144) macro(145) macro(146) macro(147) macro(148) macro(149) macro(150) macro(151) \
    macro(152) macro(153) macro(154) macro(155) macro(156) macro(157) macro(158) macro(159) \
    macro(160) macro(161) macro(162) macro(163) macro(164) macro(165) macro(166) macro(167) \
    macro(168) macro(169) macro(170) macro(171) macro(172) macro(173) macro(174) macro(175) \
    macro(176) macro(177) macro(178) macro(179) macro(180) macro(181) macro(182) macro(183) \
    macro(184) macro(185) macro(186) macro(187) macro(188) macro(189) macro(190) macro(191) \
    macro(192) macro(193) macro(194) macro(195) macro(196) macro(197) macro(198) macro(199) \
    macro(200) macro(201) macro(202) macro(203) macro(204) macro(205) macro(206) macro(207) \
    macro(208) macro(209) macro(210) macro(211) macro(212) macro(213) macro(214) macro(215) \
    macro(216) macro(217) macro(218) macro(219) macro(220) macro(221) macro(222) macro(223) \
    macro(224) macro(225) macro(226) macro(227) macro(228) macro(229) macro(230) macro(231) \
    macro(232) macro(233) macro(234) macro(235) macro(236) macro(237) macro(238) macro(239) \
    macro(240) macro(241) macro(242) macro(243) macro(244) macro(245) macro(246) macro(247) \
    macro(248) macro(249) macro(250) macro(251) macro(252) macro(253) macro(254) macro(255)

// Wrapper to ensure ISR stubs can reference a symbol
__attribute__((section(".isr_stubs"), used))
void *isr_stubs[256] = {
    [0] = isr_0,
    [1] = isr_1,
    [2] = isr_2,
    [3] = isr_3,
    [4] = isr_4,
    [5] = isr_5,
    [6] = isr_6,
    [7] = isr_7,
    [8] = isr_8,
    [9] = isr_9,
    [10] = isr_10,
    [11] = isr_11,
    [12] = isr_12,
    [13] = isr_13,
    [14] = isr_14,
    [15] = isr_15,
    [16] = isr_16,
    [17] = isr_17,
    [18] = isr_18,
    [19] = isr_19,
    [20] = isr_20,
    [21] = isr_21,
    [22] = isr_22,
    [23] = isr_23,
    [24] = isr_24,
    [25] = isr_25,
    [26] = isr_26,
    [27] = isr_27,
    [28] = isr_28,
    [29] = isr_29,
    [30] = isr_30,
    [31] = isr_31,
};