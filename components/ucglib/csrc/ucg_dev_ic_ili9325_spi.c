/*

  ucg_dev_ic_ili9325_spi.c
  
  Specific code for the ili9325 controller (TFT displays) with SPI mode (IM3=0, IM2=1, IM1=1, IM0=1)
  1 May 2014: Currently, this is not working

  Universal uC Color Graphics Library
  
  Copyright (c) 2014, olikraus@gmail.com
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, 
  are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this list 
    of conditions and the following disclaimer.
    
  * Redistributions in binary form must reproduce the above copyright notice, this 
    list of conditions and the following disclaimer in the documentation and/or other 
    materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  

*/

#include "ucg.h"


static const ucg_pgm_uint8_t ucg_ili9325_set_pos_seq[] = 
{
  UCG_CS(0),					/* enable chip */
  UCG_C10(0x020),	UCG_VARX(0,0x00, 0), UCG_VARX(0,0x0ff, 0),					/* set x position */
  UCG_C10(0x021),	UCG_VARY(8,0x01, 0), UCG_VARY(0,0x0ff, 0),		/* set y position */
  UCG_C10(0x022),							/* write to RAM */
  UCG_DATA(),								/* change to data mode */
  UCG_END()
};


static const ucg_pgm_uint8_t ucg_ili9325_set_pos_dir0_seq[] = 
{
  UCG_CS(0),					/* enable chip */
  
  /* last byte: 0x030 horizontal increment (dir = 0) */
  /* last byte: 0x038 vertical increment (dir = 1) */
  /* last byte: 0x000 horizontal deccrement (dir = 2) */
  /* last byte: 0x008 vertical deccrement (dir = 3) */
  UCG_C12(0x003, 0xc0 | 0x010, 0x030),              	/* Entry Mode, GRAM write direction and BGR (Bit 12)=1, set TRI (Bit 15) and DFM (Bit 14) --> three byte transfer */
  UCG_C10(0x020),	UCG_VARX(0,0x00, 0), UCG_VARX(0,0x0ff, 0),					/* set x position */
  UCG_C10(0x021),	UCG_VARY(8,0x01, 0), UCG_VARY(0,0x0ff, 0),		/* set y position */

  UCG_C10(0x022),							/* write to RAM */
  UCG_DATA(),								/* change to data mode */
  UCG_END()
};

static const ucg_pgm_uint8_t ucg_ili9325_set_pos_dir1_seq[] = 
{
  UCG_CS(0),					/* enable chip */
  
  /* last byte: 0x030 horizontal increment (dir = 0) */
  /* last byte: 0x038 vertical increment (dir = 1) */
  /* last byte: 0x000 horizontal deccrement (dir = 2) */
  /* last byte: 0x008 vertical deccrement (dir = 3) */
  UCG_C12(0x003, 0xc0 | 0x010, 0x038),              	/* Entry Mode, GRAM write direction and BGR (Bit 12)=1, set TRI (Bit 15) and DFM (Bit 14) --> three byte transfer */
  UCG_C10(0x020),	UCG_VARX(0,0x00, 0), UCG_VARX(0,0x0ff, 0),					/* set x position */
  UCG_C10(0x021),	UCG_VARY(8,0x01, 0), UCG_VARY(0,0x0ff, 0),		/* set y position */

  UCG_C10(0x022),							/* write to RAM */
  UCG_DATA(),								/* change to data mode */
  UCG_END()
};

static const ucg_pgm_uint8_t ucg_ili9325_set_pos_dir2_seq[] = 
{
  UCG_CS(0),					/* enable chip */
  
  /* last byte: 0x030 horizontal increment (dir = 0) */
  /* last byte: 0x038 vertical increment (dir = 1) */
  /* last byte: 0x000 horizontal deccrement (dir = 2) */
  /* last byte: 0x008 vertical deccrement (dir = 3) */
  UCG_C12(0x003, 0xc0 | 0x010, 0x000),              	/* Entry Mode, GRAM write direction and BGR (Bit 12)=1, set TRI (Bit 15) and DFM (Bit 14) --> three byte transfer */
  UCG_C10(0x020),	UCG_VARX(0,0x00, 0), UCG_VARX(0,0x0ff, 0),					/* set x position */
  UCG_C10(0x021),	UCG_VARY(8,0x01, 0), UCG_VARY(0,0x0ff, 0),		/* set y position */

  UCG_C10(0x022),							/* write to RAM */
  UCG_DATA(),								/* change to data mode */
  UCG_END()
};

static const ucg_pgm_uint8_t ucg_ili9325_set_pos_dir3_seq[] = 
{
  UCG_CS(0),					/* enable chip */
  
  /* last byte: 0x030 horizontal increment (dir = 0) */
  /* last byte: 0x038 vertical increment (dir = 1) */
  /* last byte: 0x000 horizontal deccrement (dir = 2) */
  /* last byte: 0x008 vertical deccrement (dir = 3) */
  UCG_C12(0x003, 0xc0 | 0x010, 0x008),              	/* Entry Mode, GRAM write direction and BGR (Bit 12)=1, set TRI (Bit 15) and DFM (Bit 14) --> three byte transfer */
  UCG_C10(0x020),	UCG_VARX(0,0x00, 0), UCG_VARX(0,0x0ff, 0),					/* set x position */
  UCG_C10(0x021),	UCG_VARY(8,0x01, 0), UCG_VARY(0,0x0ff, 0),		/* set y position */

  UCG_C10(0x022),							/* write to RAM */
  UCG_DATA(),								/* change to data mode */
  UCG_END()
};

static ucg_int_t ucg_handle_ili9325_l90fx(ucg_t *ucg)
{
  uint8_t c[3];
  if ( ucg_clip_l90fx(ucg) != 0 )
  {
    switch(ucg->arg.dir)
    {
      case 0: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir0_seq);	
	break;
      case 1: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir1_seq);	
	break;
      case 2: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir2_seq);	
	break;
      case 3: 
      default: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir3_seq);	
	break;
    }
    c[0] = ucg->arg.pixel.rgb.color[0];
    c[1] = ucg->arg.pixel.rgb.color[1];
    c[2] = ucg->arg.pixel.rgb.color[2];
    ucg_com_SendRepeat3Bytes(ucg, ucg->arg.len, c);
    ucg_com_SetCSLineStatus(ucg, 1);		/* disable chip */
    return 1;
  }
  return 0;
}

/*
  L2TC (Glyph Output)
  Because of this for vertical lines the x min and max values in
  "ucg_ili9325_set_pos_for_y_seq" are identical to avoid changes of the x position
  
*/

static ucg_int_t ucg_handle_ili9325_l90se(ucg_t *ucg)
{
  uint8_t i;
  uint8_t c[3];
  
  /* Setup ccs for l90se. This will be updated by ucg_clip_l90se if required */
  
  for ( i = 0; i < 3; i++ )
  {
    ucg_ccs_init(ucg->arg.ccs_line+i, ucg->arg.rgb[0].color[i], ucg->arg.rgb[1].color[i], ucg->arg.len);
  }
  
  /* check if the line is visible */
  
  if ( ucg_clip_l90se(ucg) != 0 )
  {
    ucg_int_t i;
    switch(ucg->arg.dir)
    {
      case 0: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir0_seq);	
	break;
      case 1: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir1_seq);	
	break;
      case 2: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir2_seq);	
	break;
      case 3: 
      default: 
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_dir3_seq);	
	break;
    }
    
    for( i = 0; i < ucg->arg.len; i++ )
    {
      c[0] = ucg->arg.ccs_line[0].current;
      c[1] = ucg->arg.ccs_line[1].current; 
      c[2] = ucg->arg.ccs_line[2].current;
      ucg_com_SendRepeat3Bytes(ucg, 1, c);
      ucg_ccs_step(ucg->arg.ccs_line+0);
      ucg_ccs_step(ucg->arg.ccs_line+1);
      ucg_ccs_step(ucg->arg.ccs_line+2);
    }
    ucg_com_SetCSLineStatus(ucg, 1);		/* disable chip */
    return 1;
  }
  return 0;
}


ucg_int_t ucg_dev_ic_ili9325_spi_18(ucg_t *ucg, ucg_int_t msg, void *data)
{
  switch(msg)
  {
    case UCG_MSG_DEV_POWER_UP:
      /* setup com interface and provide information on the clock speed */
      /* of the serial and parallel interface. Values are nanoseconds. */
      return ucg_com_PowerUp(ucg, 40, 100);
    case UCG_MSG_DEV_POWER_DOWN:
      /* not yet implemented */
      return 1;
    case UCG_MSG_GET_DIMENSION:
      ((ucg_wh_t *)data)->w = 240;
      ((ucg_wh_t *)data)->h = 320;
      return 1;
    case UCG_MSG_DRAW_PIXEL:
      if ( ucg_clip_is_pixel_visible(ucg) !=0 )
      {
	uint8_t c[3];
	ucg_com_SendCmdSeq(ucg, ucg_ili9325_set_pos_seq);	
	c[0] = ucg->arg.pixel.rgb.color[0];
	c[1] = ucg->arg.pixel.rgb.color[1];
	c[2] = ucg->arg.pixel.rgb.color[2];
	ucg_com_SendRepeat3Bytes(ucg, 1, c);
	ucg_com_SetCSLineStatus(ucg, 1);		/* disable chip */
      }
      return 1;
    case UCG_MSG_DRAW_L90FX:
      //ucg_handle_l90fx(ucg, ucg_dev_ic_ili9325_18);
      ucg_handle_ili9325_l90fx(ucg);
      return 1;
#ifdef UCG_MSG_DRAW_L90TC
    case UCG_MSG_DRAW_L90TC:
      //ucg_handle_l90tc(ucg, ucg_dev_ic_ili9325);
      ucg_handle_ili9325_l90tc(ucg);
      return 1;
#endif /* UCG_MSG_DRAW_L90TC */
#ifdef UCG_MSG_DRAW_L90BF
     case UCG_MSG_DRAW_L90BF:
      ucg_handle_l90bf(ucg, ucg_dev_ic_ili9325_18);
      return 1;
#endif /* UCG_MSG_DRAW_L90BF */
    /* msg UCG_MSG_DRAW_L90SE is handled by ucg_dev_default_cb */
    /*
    case UCG_MSG_DRAW_L90SE:
      return ucg->ext_cb(ucg, msg, data);
    */
  }
  return ucg_dev_default_cb(ucg, msg, data);  
}

ucg_int_t ucg_ext_ili9325_spi_18(ucg_t *ucg, ucg_int_t msg, void *data)
{
  switch(msg)
  {
    case UCG_MSG_DRAW_L90SE:
      //ucg_handle_l90se(ucg, ucg_dev_ic_ili9325);
      ucg_handle_ili9325_l90se(ucg);
      break;
  }
  return 1;
}