	cpu     8086
	org     0xf000

;;;; BLANK INTERRUPT ;;;;

	iret

;;;; FILL INTERRUPT VECTOR TABLE ;;;;

	mov     ax, cs
	mov     cx, 0x7f
	rep     stosw

	mov     word [es:0x08*4], INT08 ; TIMER	
	mov     word [es:0x09*4], INT09 ; KEYBOARD HW
	mov     word [es:0x10*4], INT10 ; VIDEO
	mov     word [es:0x12*4], INT12 ; MEMORY
	mov     word [es:0x13*4], INT13 ; DISK
	mov     word [es:0x16*4], INT16 ; KEYBOARD SW
	;mov     word [es:0x1a*4], INT1A ; TIME

;;;; SETUP KEYBOARD BUFFER ;;;;

	mov     di, 0x40
	mov     es, di
	mov     word [es:0x1a], 0x1e
	mov     word [es:0x1c], 0x1e

;;;; INITIALIZE INTERRUPT CONTROLLER ;;;;

	mov     al, 16
	out     0x20, al
	mov     al, 8
	out     0x21, al
	xor     al, al
	out     0x21, al
	out     0x21, al

;;;; INITIALIZE TIMER ;;;;

	mov     al, 0x36
	out     0x43, al
	xor     al, al
	out     0x40, al
	out     0x40, al

;;;; BOOT ;;;;

	xor     ax, ax
	mov     es, ax
	mov     ax, 0x0201
	mov     bx, 0x7c00
	mov     cx, 1
	mov     dx, 0x0080
	int     0x13
	jmp     0:0x7c00

;;;; TIMER ;;;;

INT08:
	push	ds
	push	ax
	mov     ax, 0x40
	mov     ds, ax
	clc
	adc     word [ds:0x6c], 1
	adc     word [ds:0x6e], 0
	int     0x1c
	mov     al, 0x20
	out     0x20, al
	pop     ax
	pop     ds
	iret

;;;; KEYBOARD HW ;;;;

INT09:
	push	ax
	push	bx
	push	es
	push	di
	push	ds
	in      al, 0x60
	mov     di, 0x40
	mov     es, di
	mov     di, [es:0x1c]
INT09_check_CTRL_DOWN:
	cmp     al, 0x1d
	jne     INT09_check_CTRL_UP
	or      [es:0x17], byte 00000100b
	jmp     INT09_skip_buffering
INT09_check_CTRL_UP:
	cmp     al, 0x9d
	jne     INT09_check_LSHFT_DOWN
	and     [es:0x17], byte 11111011b
	jmp     INT09_skip_buffering
INT09_check_LSHFT_DOWN:
	cmp     al, 0x2a
	jne     INT09_check_LSHFT_UP
	or      [es:0x17], byte 00000010b
	jmp     INT09_skip_buffering
INT09_check_LSHFT_UP:
	cmp     al, 0xaa
	jne     INT09_translate
	and     [es:0x17], byte 11111101b
	jmp     INT09_skip_buffering
INT09_translate:
	cmp     al, 0x7f
	ja      INT09_skip_buffering
	mov     bx, cs
	mov     ds, bx
	mov     ah, al
	mov     bx, ASCII
	xlatb
	mov     bl, [es:0x17]
	and     bl, 3
	jz      INT09_check_CTRL
	sub     al, 0x20
INT09_check_CTRL:
	mov     bl, [es:0x17]
	and     bl, 4
	jz      INT09_check_buffer_roll
	sub     al, 0x60
INT09_check_buffer_roll:
	mov     [es:di], ax
	inc     di
	inc     di
	cmp     di, 0x3e
	jb      INT09_skip_buffer_roll
	sub     di, 32
INT09_skip_buffer_roll:
	mov     [es:0x1c], di
INT09_skip_buffering:
	mov     al, 0x20
	out     0x20, al
	pop     ds
	pop     di
	pop     es
	pop     bx
	pop     ax
	iret
ASCII:
	db      0   ; 00
	db      27  ; 01 <ESC>
	db      '1' ; 02
	db      '2' ; 03
	db      '3' ; 04
	db      '4' ; 05
	db      '5' ; 06
	db      '6' ; 07
	db      '7' ; 08
	db      '8' ; 09
	db      '9' ; 0A
	db      '0' ; 0B
	db      '-' ; 0C
	db      '=' ; 0D
	db      8   ; 0E <BACKSPACE>
	db      9   ; 0F <TAB>
	db      'q' ; 10
	db      'w' ; 11
	db      'e' ; 12
	db      'r' ; 13
	db      't' ; 14
	db      'y' ; 15
	db      'u' ; 16
	db      'i' ; 17
	db      'o' ; 18
	db      'p' ; 19
	db      '[' ; 1A
	db      ']' ; 1B
	db      13  ; 1C <ENTER>
	db      0   ; 1D
	db      'a' ; 1E
	db      's' ; 1F
	db      'd' ; 20
	db      'f' ; 21
	db      'g' ; 22
	db      'h' ; 23
	db      'j' ; 24
	db      'k' ; 25
	db      'l' ; 26
	db      ';' ; 27
	db      39  ; 28
	db      '`' ; 29
	db      0   ; 2A
	db      '\' ; 2B
	db      'z' ; 2C
	db      'x' ; 2D
	db      'c' ; 2E
	db      'v' ; 2F
	db      'b' ; 30
	db      'n' ; 31
	db      'm' ; 32
	db      ',' ; 33
	db      '.' ; 34
	db      '/' ; 35
	db      0   ; 36 <SHIFT>
	db      0   ; 37
	db      0   ; 38
	db      ' ' ; 39 <SPACE>
	times 64 db 0

;;;; VIDEO ;;;;

INT10:
	cmp     ah, 0
	je      INT10_setmode
	cmp     ah, 0x0e
	je      INT10_putchar
	cmp     ax, 0x1010
	je      INT10_set_palette
	cmp     ax, 0x1015
	je      INT10_get_palette
	dw		0x040f
	mov		al, 0x1a
	mov		bx, 8
	iret
INT10_setmode:
	dw      0x030f
	iret
INT10_putchar:
	dw      0x000f
	iret
INT10_set_palette:
	mov     ah, dh
	mov		dx, 0x3c8
	mov		al, bl
	out		dx, al
	inc		dx
	mov     al, ah
	out     dx, al
	mov     al, ch
	out     dx, al
	mov     al, cl
	out     dx, al
	iret
INT10_get_palette:
	mov     dx, 0x3c7
	mov     al, bl
	out     dx, al
	inc     dx
	inc     dx
	in      al, dx
	mov     ah, al	
	in      al, dx
	mov     ch, al
	in      al, dx
	mov     cl, al
	mov     dh, ah
	iret

;;;; CONVENTIONAL MEMORY ;;;;

INT12:
	mov     ax, 640
	iret

;;;; DISK ;;;;

INT13:
	cmp     ah, 2
	je      INT13_read_disk
	cmp     ah, 3
	je      INT13_write_disk
	cmp     ah, 8
	je      INT13_disk_type
	iret
INT13_write_disk:
	dw      0x010f
	iret
INT13_read_disk:
	dw      0x020f
	iret
INT13_disk_type: ; CHS: 511/16/63
	mov     cx, 0xfd7f
	mov     dx, 0x0f01
	iret

;;;; KEYBOARD SW ;;;;

INT16:
	push	ds
	push	bx
	mov     bx, 0x40
	mov     ds, bx
	or      ah, ah
	jz      INT16_read
	dec     ah
	jz      INT16_wait
	xor     ax, ax
INT16_exit:
	pop     bx
	pop     ds
	iret
INT16_read:
	mov     bx, [ds:0x1a]
	cmp     bx, [ds:0x1c]
	jz      INT16_read
	mov     ax, [ds:bx]
	inc     bx
	inc     bx
	cmp     bx, 0x3e
	jb      INT16_got_key
	sub     bx, 32
INT16_got_key:
	mov     [ds:0x1a], bx
	jmp     INT16_exit
INT16_wait:
	cli
	mov     bx, [ds:0x1a]
	cmp     bx, [ds:0x1c]
	mov     ax, [ds:bx]
	sti
	pop     bx
	pop     ds
	retf	2

;;;; TIME ;;;;

INT1A:
	push	ax
	push	ds
	mov     ax, 0x40
	mov     ds, ax
	mov     cx, [ds:0x6e]
	mov     dx, [ds:0x6c]
	pop     ds
	pop     ax
	iret

;;;; RESET VECTOR ;;;;

	times	0xff0-($-$$) db 0
	jmp     0xf000:0xf001
	times   0x1000-($-$$) db 0xff