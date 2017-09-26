.globl DrawL0

@ void DrawL0(InnerParams* params)
@ InnerParams:
@ Pixel palette[256];
@ Pixel* line;
@ unsigned int* world_line;
@ unsigned int* inv_diff_y;
@ int* last_color;
@ int* last_y;
@ int level_2;
@ int h;

DrawL0:
 stmdb sp!, {r4,  r5,  r6,  r7,  r8,  r9,  r10, r11, r12, lr}
  ldr     r1, [r0, #532] @ level_2 -> r1

  ldr     r4, [r0, #512]  @ line -> r4
  ldr     r5, [r0, #516]  @ world_line -> r5
  ldr     r6, [r0, #520] @ inv_diff_y -> r6
  ldr     r7, [r0, #524] 
  ldr     r7, [r7]      @ last_color -> r7
  ldr     r8, [r0, #528]
  ldr     r8, [r8]      @ last_y


  str     sp, [r0, #536] @saving sp in "h"

  mov     r9, #0        @ y = 0 -> r9
  
For_0:                  @ do 
 
  ldr   r10, [r5, r9, lsl #2]  @ texel = world_line[y * 4]
  mov   r11, r10, lsr #8       @ current_y = texel >> 8
  and   r10, r10, #255         @ texel = texel & 0xFF
  mov   r10, r10, lsl #12      @ int current_color = (texel & 0xFF)<<12; 

  cmp   r11, r8               
  ble   For_0_Guard            @ if (current_y <= last_y) jump
  
  sub   r12, r11, r8           @ r12 = current_y - last_y
  ldr   r12, [r6, r12, lsl #2] @ diff_y = inv_diff_y[current_y-last_y];

  sub   r14, r10, r7           @ current_color - last_color
  mov   r14, r14, lsr #12      @ delta_color = (current_color - last_color) >> 12
  mul   r14, r14, r12          @ delta_color = delta_color * diff_y

  mov   r12, r7                @ color = last_color
  cmp   r11, #512              @current_y = GetMin(current_y, h - 1);
  movge r11, #512
  subge r11, #1
  
  sub   r3, r11, #1            @ r3 = current_y - 1
  mov   r2, r8                 @ y0 = last_y

For_1:
  sub   r3, r11, #1            @ r3 = current_y - 1
  cmp   r2, r3
  bgt   For_1_End
  
  ldr   r3, [r0, r12, lsr #11] @ r3 = palette[(color>>12)*2]
  mov   r3, r3, lsr #16
  
  mov   sp, r2, lsl #1         @ sp = r2 * 2
  strh  r3, [r4, sp]           @ line[y0] = r3   
  
  add r2, r2, #1
  b For_1
  
For_1_End:
  mov r8, r11                  @last_y = current_y;
  mov r7, r10                  @last_color = current_color;

For_0_Guard:
  add   r9, r9, #1       @ y++
  cmp   r9, r1           @ while ( y < level 2 && last_y < h );
  cmplt r8, #512
  blt   For_0
  
  ldr     r3, [r0, #524] 
  ldr     r1, [r0, #528]
    
  str     r8, [r1] @ *last_y_ptr = last_y;
  str     r7, [r3] @ *last_color_ptr = last_color;
 
  ldr     sp, [r0, #536]

  ldmia sp!,  {r4,  r5,  r6,  r7,  r8,  r9,  r10, r11, r12, lr}
  bx  lr 



