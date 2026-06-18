    .syntax unified
    .cpu cortex-m0
    .thumb

    .section .text.Reset_Handler
    .globl Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_estack
    mov sp, r0
    bl SystemInit
    bl main
    b .

    .section .text.Default_Handler
    .globl Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b .

    .macro def_irq_handler handler_name
    .weak \handler_name
    .thumb_set \handler_name, Default_Handler
    .endm

    def_irq_handler NMI_Handler
    def_irq_handler HardFault_Handler
    def_irq_handler SVC_Handler
    def_irq_handler PendSV_Handler
    def_irq_handler SysTick_Handler
    def_irq_handler IRQ0_IRQHandler
    def_irq_handler IRQ1_IRQHandler
    def_irq_handler IRQ2_IRQHandler
    def_irq_handler IRQ3_IRQHandler
    def_irq_handler IRQ4_IRQHandler
    def_irq_handler IRQ5_IRQHandler
    def_irq_handler IRQ6_IRQHandler
    def_irq_handler IRQ7_IRQHandler
    def_irq_handler IRQ8_IRQHandler
    def_irq_handler TIMER1_IRQHandler
    def_irq_handler IRQ10_IRQHandler
    def_irq_handler IRQ11_IRQHandler
    def_irq_handler IRQ12_IRQHandler
    def_irq_handler IRQ13_IRQHandler
    def_irq_handler IRQ14_IRQHandler
    def_irq_handler IRQ15_IRQHandler
    def_irq_handler IRQ16_IRQHandler
    def_irq_handler IRQ17_IRQHandler
    def_irq_handler IRQ18_IRQHandler
    def_irq_handler IRQ19_IRQHandler
    def_irq_handler IRQ20_IRQHandler
    def_irq_handler IRQ21_IRQHandler
    def_irq_handler IRQ22_IRQHandler
    def_irq_handler IRQ23_IRQHandler
    def_irq_handler IRQ24_IRQHandler
    def_irq_handler IRQ25_IRQHandler
    def_irq_handler IRQ26_IRQHandler
    def_irq_handler IRQ27_IRQHandler
    def_irq_handler IRQ28_IRQHandler
    def_irq_handler IRQ29_IRQHandler
    def_irq_handler IRQ30_IRQHandler
    def_irq_handler IRQ31_IRQHandler

    .section .isr_vector
    .globl _vector_table
_vector_table:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word 0
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .word IRQ0_IRQHandler
    .word IRQ1_IRQHandler
    .word IRQ2_IRQHandler
    .word IRQ3_IRQHandler
    .word IRQ4_IRQHandler
    .word IRQ5_IRQHandler
    .word IRQ6_IRQHandler
    .word IRQ7_IRQHandler
    .word IRQ8_IRQHandler
    .word TIMER1_IRQHandler
    .word IRQ10_IRQHandler
    .word IRQ11_IRQHandler
    .word IRQ12_IRQHandler
    .word IRQ13_IRQHandler
    .word IRQ14_IRQHandler
    .word IRQ15_IRQHandler
    .word IRQ16_IRQHandler
    .word IRQ17_IRQHandler
    .word IRQ18_IRQHandler
    .word IRQ19_IRQHandler
    .word IRQ20_IRQHandler
    .word IRQ21_IRQHandler
    .word IRQ22_IRQHandler
    .word IRQ23_IRQHandler
    .word IRQ24_IRQHandler
    .word IRQ25_IRQHandler
    .word IRQ26_IRQHandler
    .word IRQ27_IRQHandler
    .word IRQ28_IRQHandler
    .word IRQ29_IRQHandler
    .word IRQ30_IRQHandler
    .word IRQ31_IRQHandler
