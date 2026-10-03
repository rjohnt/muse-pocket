# SPDX-License-Identifier: Apache-2.0
async def request_cards(session, state):
    state.set(cards_request='pending')
    try:
        result = await session.send_chat(
            'Populate this Mac display gadget with my actual current open watches and next upcoming event. '
            'Call pocket.set_watch_digest and pocket.set_next_up using their registered schemas; '
            'each payload argument must be a serialized JSON string. Use timezone-qualified ISO timestamps. '
            'The next event needs when and ends; if the end is unavailable, explain that rather than inventing it. '
            'Curate concise labels and notes for a small display. Do not include account numbers or credentials. '
            'Use only data you can access now. If there are no watches send an empty items list; '
            'if there is no upcoming event send an empty title. Explain any failed command in chat.'
        )
        state.set(cards_request='sent' if result.get('ok') else 'failed')
    except Exception:
        state.set(cards_request='failed')
