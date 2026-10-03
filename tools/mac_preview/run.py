#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Muse Pocket application preview, using the independent macOS adapter."""
from pathlib import Path
import sys
# Support invoking this script directly from a checkout.
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from muse_mac.host import Application, main
from tools.mac_preview.pocket_commands import COMMANDS, DisplayExecutor, DisplayState
from tools.mac_preview.office import OfficePack
from tools.mac_preview.requests import request_cards


def configure(parser):
    parser.add_argument('--request-cards',action='store_true',help='Ask Muse once for current watches and the next event')

async def registered(session,state,args):
    if args.request_cards: await request_cards(session,state)

def application(state_factory=DisplayState):
    return Application(caption_command='pocket.set_status',state_factory=state_factory,executor_factory=DisplayExecutor,
                       command_specs=COMMANDS,preview_path=Path(__file__).with_name('preview.html'),
                       routes={'/office':OfficePack().route},configure_parser=configure,on_registered=registered)

if __name__=='__main__':raise SystemExit(main(application()))
