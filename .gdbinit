set print pretty on
set pagination off
set output-radix 16

set logging file /tmp/gdb_output.log
set logging on
set logging overwrite on

define setup
  shell rm -f /tmp/inferior_output.log
  shell touch /tmp/inferior_output.log
  set inferior-tty /tmp/inferior_output.log

  set environment PD /home/catherine/src/pd/perfect-dark-foj
  file /home/catherine/src/pd/perfect-dark-foj/build/pd.x86_64
end

set args \
  --moddir $PD_MODDIR/mod_fojo \
  --savedir $PD_SAVEDIR \
  --basedir $PD_BASEDIR \
  --rom-file $PD_ROMFILE \
  --skip-intro

set args \
  --moddir $PD_MODDIR/mod_aio \
  --moddir $PD_MODDIR/mod_gex \
  --moddir $PD_MODDIR/mod_kakariko \
  --moddir $PD_MODDIR/mod_dark_noon \
  --moddir $PD_MODDIR/mod_goldfinger_64 \
  --moddir $PD_MODDIR/mod_fojo \
  --savedir $PD_SAVEDIR \
  --basedir $PD_BASEDIR \
  --rom-file $PD_ROMFILE \
  --skip-intro


# set logging for stderr and stdout
set logging on
set logging redirect on
set logging overwrite on
set logging file /tmp/pd.gdb.log

# TODO: set invincible
# TODO: set invisible

define kill_player 
  if $argc != 1
    printf "kill_player: Usage: kill_player <player_id>\n"
    return
  end 
  set g_Vars.players[$arg0].isdead = 1
end

define unlock_mouse
  call inputLockMouse(0)
end

define load_coop_player
  if $argc != 1
    printf "load_player: Usage: load_player <player_id>\n"
    return
  end 

  # this logic was copied from lv.c:lvReset
  call playermgrAllocatePlayer($arg0)

  set $lastplayer = g_Vars.currentplayernum
  call setCurrentPlayerNum($arg0)
  set g_Vars.currentplayer->usedowntime = 0
  set g_Vars.currentplayer->invdowntime = g_Vars.currentplayer->usedowntime

  call menuReset()
  call amReset()
  call invReset()
  call bgunReset()
  call playerLoadDefaults()
  call playerReset()
  call playerSpawn()
  call bheadReset()


  call setCurrentPlayerNum($lastplayer)
end

define load_anti_player
  if $argc != 1
    printf "load_player: Usage: load_player <player_id>\n"
    return
  end 

  # this logic was copied from lv.c:lvReset
  call playermgrAllocatePlayer($arg0)

  set $lastplayer = g_Vars.currentplayernum
  call setCurrentPlayerNum($arg0)
  set g_Vars.antiplayernum = $arg0
  set g_Vars.antiplayers[$arg0] = g_Vars.currentplayer
  set g_Vars.currentplayer->usedowntime = 0
  set g_Vars.currentplayer->invdowntime = g_Vars.currentplayer->usedowntime

  call menuReset()
  call amReset()
  call invReset()
  call bgunReset()
  call playerLoadDefaults()
  call playerReset()
  call playerSpawn()
  call bheadReset()



  call setCurrentPlayerNum($lastplayer)
end

# vim: set ft=gdb tabstop=2 shiftwidth=2 expandtab:
