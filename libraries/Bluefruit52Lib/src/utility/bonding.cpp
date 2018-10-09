/**************************************************************************/
/*!
    @file     bonding.cpp
    @author   hathach (tinyusb.org)

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2018, Adafruit Industries (adafruit.com)
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**************************************************************************/

#include <Arduino.h>
#include "bonding.h"

#include "flash/flash_nrf52.h"
#include "bluefruit.h"

/*------------------------------------------------------------------*/
/* Bond Key is saved in following layout
 * - Bond Data : 80 bytes
 * - Name      : variable (including null char)
 *
 * CCCD is saved separately
 * - CCCD      : variable
 *
 * Each field has an 1-byte preceding length
 *------------------------------------------------------------------*/

#ifdef NRF52840_XXAA
#define BOND_FLASH_ADDR     0xF3000
#else
#define BOND_FLASH_ADDR     0x73000
#endif

// TODO make it dynamic later
#define MAX_BONDS   16

#define SVC_CONTEXT_FLAG                 (BLE_GATTS_SYS_ATTR_FLAG_SYS_SRVCS | BLE_GATTS_SYS_ATTR_FLAG_USR_SRVCS)

#if CFG_DEBUG >= 2
#define printBondDir(role)    dbgPrintDir( role == BLE_GAP_ROLE_PERIPH ? BOND_DIR_PRPH : BOND_DIR_CNTR )
#else
#define printBondDir(role)
#endif

void bond_init(void)
{

}

/*------------------------------------------------------------------*/
/* Keys
 *------------------------------------------------------------------*/
static void bond_save_keys_dfr (uint8_t role, uint16_t conn_hdl, bond_data_t* bdata)
{
  uint16_t const ediv = (role == BLE_GAP_ROLE_PERIPH) ? bdata->own_enc.master_id.ediv : bdata->peer_enc.master_id.ediv;

  //------------- save keys -------------//
  uint32_t fl_addr = BOND_FLASH_ADDR;
  uint8_t const keylen = sizeof(bond_data_t);

  fl_addr += flash_nrf52_write8(fl_addr, sizeof(bond_data_t));
  fl_addr += flash_nrf52_write(fl_addr, bdata, sizeof(bond_data_t));

  //------------- save device name -------------//
  char devname[64] = { 0 };
  uint8_t namelen = Bluefruit.Gap.getPeerName(conn_hdl, devname, sizeof(devname));

  // If couldn't get devname use peer mac address
  if ( namelen == 0 )
  {
    uint8_t* mac = bdata->peer_id.id_addr_info.addr;
    namelen = sprintf(devname, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  }

  fl_addr += flash_nrf52_write8(fl_addr, namelen + 1);    // also include null char
  fl_addr += flash_nrf52_write(fl_addr, devname, namelen);
  fl_addr += flash_nrf52_write8(fl_addr, 0);    // null char

  flash_nrf52_flush();

  LOG_LV2("BOND", "Keys for \"%s\" is saved", devname);

#if 0
  char filename[BOND_FNAME_LEN];
  get_fname(filename, role, role == BLE_GAP_ROLE_PERIPH ? bdata->own_enc.master_id.ediv : bdata->peer_enc.master_id.ediv);

  char devname[CFG_MAX_DEVNAME_LEN] = { 0 };
  Bluefruit.Gap.getPeerName(conn_hdl, devname, CFG_MAX_DEVNAME_LEN);

  NffsFile file(filename, FS_ACCESS_WRITE);

  VERIFY( file.exists(), );

  bool result = true;

  // write keys
  if ( !file.write((uint8_t*)bdata, sizeof(bond_data_t)) )
  {
    result = false;
  }

  // If couldn't get devname use peer mac address
  if ( !devname[0] )
  {
    uint8_t* mac = bdata->peer_id.id_addr_info.addr;
    sprintf(devname, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  }

  file.write((uint8_t*) devname, CFG_MAX_DEVNAME_LEN);
  file.close();

  if (result)
  {
    LOG_LV2("BOND", "Keys for \"%s\" is saved to file %s", devname, filename);
  }else
  {
    LOG_LV1("BOND", "Failed to save keys for \"%s\"", devname);
  }

  printBondDir(role);
#endif
}

bool bond_save_keys (uint8_t role, uint16_t conn_hdl, bond_data_t* bdata)
{
  uint8_t* buf = (uint8_t*) rtos_malloc( sizeof(bond_data_t) );
  VERIFY(buf);

  memcpy(buf, bdata, sizeof(bond_data_t));

  // queue to execute in Ada Callback thread
  ada_callback(buf, bond_save_keys_dfr, role, conn_hdl, buf);

  return true;
}

bool bond_load_keys(uint8_t role, uint16_t ediv, bond_data_t* bdata)
{
  uint32_t fl_addr = BOND_FLASH_ADDR;
  uint8_t const keylen = flash_nrf52_read8(fl_addr);

  fl_addr++;
  flash_nrf52_read(bdata, fl_addr, keylen);

  return true;

#if 0
  char filename[BOND_FNAME_LEN];
  get_fname(filename, role, ediv);

  bool result = (Nffs.readFile(filename, bdata, sizeof(bond_data_t)) > 0);

  if ( result )
  {
    LOG_LV2("BOND", "Load Keys from file %s", filename);
  }else
  {
    LOG_LV1("BOND", "Keys not found");
  }
  return result;
#endif
}


/*------------------------------------------------------------------*/
/* CCCD
 *------------------------------------------------------------------*/
static void bond_save_cccd_dfr (uint8_t role, uint16_t conn_hdl, uint16_t ediv)
{
  uint16_t alen = 0;
  sd_ble_gatts_sys_attr_get(conn_hdl, NULL, &alen, SVC_CONTEXT_FLAG);

  uint8_t attr[alen];

  VERIFY(ERROR_NONE == sd_ble_gatts_sys_attr_get(conn_hdl, attr, &alen, SVC_CONTEXT_FLAG),);

  (void) role;
  uint32_t fl_addr = BOND_FLASH_ADDR + 256;

  flash_nrf52_write8(fl_addr++, alen);
  flash_nrf52_write(fl_addr, attr, alen);

  flash_nrf52_flush();

  LOG_LV2("BOND", "CCCD setting is saved");

#if 0
  uint16_t len=0;
  sd_ble_gatts_sys_attr_get(conn_hdl, NULL, &len, SVC_CONTEXT_FLAG);

  uint8_t* sys_attr = (uint8_t*) rtos_malloc( len );
  VERIFY( sys_attr, );

  if ( ERROR_NONE == sd_ble_gatts_sys_attr_get(conn_hdl, sys_attr, &len, SVC_CONTEXT_FLAG) )
  {
    // save to file
    char filename[BOND_FNAME_LEN];
    get_fname(filename, role, ediv);

    if ( Nffs.writeFile(filename, sys_attr, len, BOND_FILE_CCCD_OFFSET) )
    {
      LOG_LV2("BOND", "CCCD setting is saved to file %s", filename);
    }else
    {
      LOG_LV1("BOND", "Failed to save CCCD setting");
    }

  }

  rtos_free(sys_attr);
  printBondDir(role);
#endif
}

bool bond_save_cccd (uint8_t role, uint16_t conn_hdl, uint16_t ediv)
{
  VERIFY(ediv != 0xFFFF);

  // queue to execute in Ada Callback thread
  ada_callback(NULL, bond_save_cccd_dfr, role, conn_hdl, ediv);

  return true;
}


bool bond_load_cccd(uint8_t role, uint16_t cond_hdl, uint16_t ediv)
{
  (void) role;
  uint32_t fl_addr = BOND_FLASH_ADDR + 256;
  uint16_t alen = flash_nrf52_read8(fl_addr++);

  uint8_t attr[alen];
  flash_nrf52_read(attr, fl_addr, alen);

  if ( ERROR_NONE != sd_ble_gatts_sys_attr_set(cond_hdl, attr, alen, SVC_CONTEXT_FLAG) )
  {
    sd_ble_gatts_sys_attr_set(cond_hdl, NULL, 0, 0);
    return false;
  }

  return true;

#if 0
  bool loaded = false;

  char filename[BOND_FNAME_LEN];
  get_fname(filename, role, ediv);

  NffsFile file(filename, FS_ACCESS_READ);

  if ( file.exists() )
  {
    int32_t len = file.size() - BOND_FILE_CCCD_OFFSET;

    if ( len )
    {
      uint8_t* sys_attr = (uint8_t*) rtos_malloc( len );

      if (sys_attr)
      {
        file.seek(BOND_FILE_CCCD_OFFSET);

        if ( file.read(sys_attr, len ) )
        {
          if (ERROR_NONE == sd_ble_gatts_sys_attr_set(cond_hdl, sys_attr, len, SVC_CONTEXT_FLAG) )
          {
            loaded = true;

            LOG_LV2("BOND", "Load CCCD from file %s", filename);
          }else
          {
            LOG_LV1("BOND", "CCCD setting not found");
          }
        }

        rtos_free(sys_attr);
      }
    }
  }

  file.close();

  if ( !loaded )
  {
    sd_ble_gatts_sys_attr_set(cond_hdl, NULL, 0, 0);
  }

  return loaded;
#endif
}

void bond_print_list(uint8_t role)
{
#if 0
  char const * dpath = (role == BLE_GAP_ROLE_PERIPH ? BOND_DIR_PRPH : BOND_DIR_CNTR);

  NffsDir dir(dpath);
  NffsDirEntry dirEntry;

  while( dir.read(&dirEntry) )
  {
    if ( !dirEntry.isDirectory() )
    {
      char name[64];
      dirEntry.getName(name, sizeof(name));

      cprintf("  %s : ", name);

      // open file to read device name
      NffsFile file(dpath, dirEntry, FS_ACCESS_READ);

      varclr(name);

      file.seek(BOND_FILE_DEVNAME_OFFSET);
      if ( file.read(name, CFG_MAX_DEVNAME_LEN) )
      {
        cprintf(name);
      }

      cprintf("\n");
      file.close();
    }
  }
  cprintf("\n");
#endif
}


bool bond_find_cntr(ble_gap_addr_t* addr, bond_data_t* bdata)
{
  bool found = false;

#if 0
  NffsDir dir(BOND_DIR_CNTR);
  NffsDirEntry dirEntry;

  while( dir.read(&dirEntry) && !found )
  {
    // Read bond data of each stored file
    if ( !dirEntry.isDirectory() )
    {
      NffsFile file(BOND_DIR_CNTR, dirEntry, FS_ACCESS_READ);
      if ( file.read( (uint8_t*)bdata, sizeof(bond_data_t)) )
      {
        if ( !memcmp(addr->addr, bdata->peer_id.id_addr_info.addr, 6) )
        {
          // Compare static address
          found = true;
        }else if ( addr->addr_type == BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE )
        {
          // Resolving private address
        }
      }

      file.close();
    }
  }
#endif

  return found;
}

/*------------------------------------------------------------------*/
/* DELETE
 *------------------------------------------------------------------*/
void bond_clear_prph(void)
{
#if 0
  // Detele bonds dir
  Nffs.remove(BOND_DIR_PRPH);

  // Create an empty one
  (void) Nffs.mkdir_p(BOND_DIR_PRPH);
#endif
}

void bond_clear_cntr(void)
{
#if 0
  // Detele bonds dir
  Nffs.remove(BOND_DIR_CNTR);

  // Create an empty one
  (void) Nffs.mkdir_p(BOND_DIR_CNTR);
#endif

}


void bond_clear_all(void)
{
#if 0
  // Detele bonds dir
  Nffs.remove(BOND_DIR_ROOT);

  // Create an empty one for prph and central
  (void) Nffs.mkdir_p(BOND_DIR_PRPH);
  (void) Nffs.mkdir_p(BOND_DIR_CNTR);
#endif

}

void bond_remove_key(uint8_t role, uint16_t ediv)
{
#if 0
  char filename[BOND_FNAME_LEN];
  get_fname(filename, role, ediv);

  Nffs.remove(filename);
#endif

}
