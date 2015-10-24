#include <r_util.h>
#include <r_types.h>
#include <r_asm.h>
#include <string.h>

static void str_op(char *c) {
	if ((c[0] <= 'Z') && (c[0] >= 'A'))
		c[0] += 0x20;
}

static int gb_reg_idx (char r) {
	if (r == 'b')
		return 0;
	if (r == 'c')
		return 1;
	if (r == 'd')
		return 2;
	if (r == 'e')
		return 3;
	if (r == 'h')
		return 4;
	if (r == 'l')
		return 5;
	if (r == 'a')
		return 7;
	return -1;
}

static int gb_parse_arith1 (ut8 *buf, const int minlen, char *buf_asm, ut8 base, ut8 alt) {
	int i;
	ut64 num;
	if (strlen (buf_asm) < minlen)
		return 0;
	buf[0] = base;
	i = strlen (&buf_asm[minlen - 1]);
	r_str_replace_in (&buf_asm[minlen - 1], (ut32)i, "[ ", "[", R_TRUE);
	r_str_replace_in (&buf_asm[minlen - 1], (ut32)i, " ]", "]", R_TRUE);
	r_str_do_until_token (str_op, buf_asm, ' ');
	i = gb_reg_idx (buf_asm[minlen-1]);
	if (i != (-1))
		buf[0] |= (ut8)i;
	else if (buf_asm[minlen - 1] == '['
		&& buf_asm[minlen] == 'h'
		&& buf_asm[minlen + 1] == 'l'
		&& buf_asm[minlen + 2] == ']' )
		buf[0] |= 6;
	else {
		buf[0] = alt;
		num = r_num_get (NULL, &buf_asm[minlen - 1]);
		buf[1] = (ut8)(num & 0xff);
		return 2;
	}
	return 1;
}

static int gbAsm(RAsm *a, RAsmOp *op, const char *buf) {
	int mn_len, i, len = 1;
	ut32 mn = 0;
	ut64 num;
	if (!a || !op || !buf)
		return 0;
	strncpy (op->buf_asm, buf, R_ASM_BUFSIZE-1);
	op->buf_asm[R_ASM_BUFSIZE-1] = 0;
	i = strlen (op->buf_asm);
	r_str_replace_in (op->buf_asm, (ut32)i, "  ", " ", R_TRUE);
	r_str_replace_in (op->buf_asm, (ut32)i, " ,", ",", R_TRUE);
	mn_len = r_str_do_until_token (str_op, op->buf_asm, ' ');
	if (mn_len < 2 || mn_len > 4)
		return 0;
	for (i = 0; i < mn_len; i++)
		mn = (mn << 8) | op->buf_asm[i];
	switch (mn) {
		case 0x6e6f70:			//nop
			op->buf[0] = 0x00;
			break;
		case 0x726c6361:		//rlca
			op->buf[0] = 0x07;
			break;
		case 0x72726361:		//rrca
			op->buf[0] = 0xf0;
			break;
		case 0x73746f70:		//stop
			op->buf[0] = 0x10;
			break;
		case 0x726c61:			//rla
			op->buf[0] = 0x17;
			break;
		case 0x727261:			//rra
			op->buf[0] = 0x1f;
			break;
		case 0x646161:			//daa
			op->buf[0] = 0x27;
			break;
		case 0x63706c:			//cpl
			op->buf[0] = 0x2f;
			break;
		case 0x616463:			//adc
			len = gb_parse_arith1 (op->buf, 5, op->buf_asm, 0x88, 0xce);
			break;
		case 0x737562:			//sub
			len = gb_parse_arith1 (op->buf, 5, op->buf_asm, 0x90, 0xd6);
			break;
		case 0x736263:			//sbc
			len = gb_parse_arith1 (op->buf, 5, op->buf_asm, 0x98, 0xde);
			break;
		case 0x616e64:			//and
			len = gb_parse_arith1 (op->buf, 5, op->buf_asm, 0xa0, 0xe6);
			break;
		case 0x786f72:			//xor
			len = gb_parse_arith1 (op->buf, 5, op->buf_asm, 0xa8, 0xee);
			break;
		case 0x6f72:			//or
			len = gb_parse_arith1 (op->buf, 4, op->buf_asm, 0xb0, 0xf6);
			break;
		case 0x6370:			//cp
			len = gb_parse_arith1 (op->buf, 4, op->buf_asm, 0xb8, 0xfe);
			break;
		case 0x736366:			//scf
			op->buf[0] = 0x37;
			break;
		case 0x636366:			//ccf
			op->buf[0] = 0x3f;
			break;
		case 0x68616c74:		//halt
			op->buf[0] = 0x76;
			break;
		case 0x726574:			//ret
			if (strlen(op->buf_asm) < 5)
				op->buf[0] = 0xc9;
			else if (strlen (op->buf_asm) < 6) {	//there is no way that there can be "  " - we did r_str_replace_in
				str_op(&op->buf_asm[4]);
				if (op->buf_asm[4] == 'z')	//ret Z
					op->buf[0] = 0xc8;
				else if (op->buf_asm[4] == 'c')	//ret C
					op->buf[0] = 0xd8;
				else	return op->size = 0;
			} else {
				str_op(&op->buf_asm[4]);
				if (op->buf_asm[4] != 'n')
					return op->size = 0;
				str_op(&op->buf_asm[5]);	//if (!(strlen(op->buf_asm) < 6)) => must be 6 or greater
				if (op->buf_asm[5] == 'z')	//ret nZ
					op->buf[0] = 0xc0;
				else if (op->buf_asm[5] == 'c')	//ret nC
					op->buf[0] = 0xd0;
				else	return op->size = 0;
			}
			break;
		case 0x72657469:		//reti
			op->buf[0] = 0xd9;
			break;
		case 0x6469:			//di
			op->buf[0] = 0xf3;
			break;
		case 0x6569:			//ei
			op->buf[0] = 0xfb;
			break;
		case 0x727374:			//rst
			if (strlen (op->buf_asm) < 5)
				return op->size = 0;
			num = r_num_get (NULL, &op->buf_asm[4]);
			if ((num & 7) || ((num/8) > 7))
				return op->size = 0;
			op->buf[0] = (ut8)((num & 0xff) + 0xc7);
			break;
		case 0x70757368:		//push
			if (strlen (op->buf_asm) < 7)
				return op->size = 0;
			str_op (&op->buf_asm[5]);
			str_op (&op->buf_asm[6]);
			if (op->buf_asm[5] == 'b' && op->buf_asm[6] == 'c') {
				op->buf[0] = 0xc5;
			} else if (op->buf_asm[5] == 'd' && op->buf_asm[6] == 'e') {
				op->buf[0] = 0xd5;
			} else if (op->buf_asm[5] == 'h' && op->buf_asm[6] == 'l') {
				op->buf[0] = 0xe5;
			} else if (op->buf_asm[5] == 'a' && op->buf_asm[6] == 'f') {
				op->buf[0] = 0xf5;
			} else len = 0;
			break;
		case 0x706f70:			//pop	
			if (strlen (op->buf_asm) < 6)
				return op->size = 0;
			str_op (&op->buf_asm[4]);
			str_op (&op->buf_asm[5]);
			if (op->buf_asm[4] == 'b' && op->buf_asm[5] == 'c') {
				op->buf[0] = 0xc1;
			} else if (op->buf_asm[4] == 'd' && op->buf_asm[5] == 'e') {
				op->buf[0] = 0xd1;
			} else if (op->buf_asm[4] == 'h' && op->buf_asm[5] == 'l') {
				op->buf[0] = 0xe1;
			} else if (op->buf_asm[4] == 'a' && op->buf_asm[5] == 'f') {
				op->buf[0] = 0xf1;
			} else len = 0;
			break;
		case 0x6a70:			//jp
			if (strlen(op->buf_asm) < 4)
				return op->size = 0;
			{
				char *p = strchr (op->buf_asm, (int)',');
				if (!p) {
					str_op (&op->buf_asm[3]);
					str_op (&op->buf_asm[4]);
					if (op->buf_asm[3] == 'h' && op->buf_asm[4] == 'l')
						op->buf[0] = 0xe9;
					else {
						num = r_num_get (NULL, &op->buf_asm[3]);
						len = 3;
						op->buf[0] = 0xc3;
						op->buf[1] = (ut8)(num & 0xff);
						op->buf[2] = (ut8)((num & 0xff00) >> 8);
					}
				} else {
					str_op (p-2);
					str_op (p-1);
					if (*(p-2) == 'n') {
						if (*(p-1) == 'z')
							op->buf[0] = 0xc2;
						else if (*(p-1) == 'c')
							op->buf[0] = 0xd2;
						else	return op->size = 0;
					} else if (*(p-2) == ' ') {
						if (*(p-1) == 'z')
							op->buf[0] = 0xca;
						else if (*(p-1) == 'c')
							op->buf[0] = 0xda;
						else	return op->size = 0;
					} else	return op->size = 0;
					r_str_replace_in (p, strlen(p), ", ", ",", R_TRUE);
					if (p[1] == '\0')
						return op->size = 0;
					num = r_num_get (NULL, p + 1);
					op->buf[1] = (ut8)(num & 0xff);
					op->buf[2] = (ut8)((num & 0xff00) >> 8);
					len = 3;
				}
			}
			break;
		case 0x6a72:			//jr
			if (strlen (op->buf_asm) < 4)
				return op->size = 0;
			{
				char *p = strchr (op->buf_asm, (int)',');
				if (!p) {
					num = r_num_get (NULL, &op->buf_asm[3]);
					len = 2;
					op->buf[0] = 0x18;
					op->buf[1] = (ut8)(num & 0xff);
				} else {
					str_op (p-2);
					str_op (p-1);
					if (*(p-2) == 'n') {
						if (*(p-1) == 'z')
							op->buf[0] = 0x20;
						else if (*(p-1) == 'c')
							op->buf[0] = 0x30;
						else	return op->size = 0;
					} else if (*(p-2) == ' ') {
						if (*(p-1) == 'z')
							op->buf[0] = 0x28;
						else if (*(p-1) == 'c')
							op->buf[0] = 0x38;
						else	return op->size = 0;
					} else	return op->size = 0;
					r_str_replace_in (p, strlen(p), ", ", ",", R_TRUE);
					if (p[1] == '\0')
						return op->size = 0;
					num = r_num_get (NULL, p + 1);
					op->buf[1] = (ut8)(num & 0xff);
					len = 2;
				}
			}
			break;
		case 0x63616c6c:		//call 
			if (strlen(op->buf_asm) < 6)
				return op->size = 0;
			{
				char *p = strchr (op->buf_asm, (int)',');
				if (!p) {
					num = r_num_get (NULL, &op->buf_asm[3]);
					len = 3;
					op->buf[0] = 0xcd;
					op->buf[1] = (ut8)(num & 0xff);
					op->buf[2] = (ut8)((num & 0xff00) >> 8);
				} else {
					str_op (p-2);
					str_op (p-1);
					if (*(p-2) == 'n') {
						if (*(p-1) == 'z')
							op->buf[0] = 0xc4;
						else if (*(p-1) == 'c')
							op->buf[0] = 0xd4;
						else	return op->size = 0;
					} else if (*(p-2) == ' ') {
						if (*(p-1) == 'z')
							op->buf[0] = 0xcc;
						else if (*(p-1) == 'c')
							op->buf[0] = 0xdc;
						else	return op->size = 0;
					} else	return op->size = 0;
					r_str_replace_in (p, strlen(p), ", ", ",", R_TRUE);
					if (p[1] == '\0')
						return op->size = 0;
					num = r_num_get (NULL, p + 1);
					op->buf[1] = (ut8)(num & 0xff);
					op->buf[2] = (ut8)((num & 0xff00) >> 8);
					len = 3;
				}
			}
			break;
		default:
			len = 0;
			break;
	}
	return op->size = len;
}
