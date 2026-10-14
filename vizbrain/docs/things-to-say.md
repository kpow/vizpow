# Things to say

Everything vizBot can do, with one example each. Say it your own way; these are starting points.

## How to talk to him

- **Tap the front of his head**, wait for "Listening...", then speak.
- **Tap the front again** to cancel or to stop him mid-answer.
- **Answer his questions without tapping.** When he ends on a question, he beeps and listens for your answer (up to 3 times in a row). Say nothing and he shows "Okay." and stops. Needs bot firmware 3.7.33 or newer.
- **Type instead** at [http://localhost:4050](http://localhost:4050) on your Mac. He says the answer out loud.
- **Tap the back of his head** and he shakes it. That's a reflex; it doesn't start a conversation.
- **Hold your hand on his head for 2 seconds** for chill mode. He stops greeting you and reacting to movement for a while.

"Hey vizBot" is not included in this version. Tap his head.

## Works for everyone

| What | Try saying |
|---|---|
| Chat and questions | "Why is the sky blue?" |
| Look something up | "Who won the game last night?" He says "let me check" and takes a few seconds longer. |
| Weather | "Do I need a jacket tomorrow?" |
| See | "What am I holding?" He takes a photo with the camera in his head. |
| Remember | "Remember that my sister's name is Dana." |
| Forget | "Forget about Dana." |
| Routines | "Every weekday at 8:30, say good morning with the weather." |
| Remove a routine | "Remove the morning routine." |
| Move his head | "Look left." "Look up at me." "Nod." |
| Base lights | "Make your ring purple." "Do the rainbow ring." |
| Sounds | "Play a fanfare." "Laugh." |

**Ring animations:** breathing, rainbow, chase, fire, twinkle, pulse, aurora, mood, audio, off.

**Sounds:** chime, pop, chirp, notify, success, error, level up, coin, wind chime, heartbeat, mystery, fanfare, power up, power down, ping, whistle, confirm, happy hum, sad sigh, curious, laugh, yawn, and three short songs (funky, chill, retro).

**Faces:** he picks a face for every answer by himself. You don't ask for one.

**Personality:** chill, hyper or grumpy. Choose it on the bot's own web page, under Personality. It changes how he talks, not his voice.

## He notices you

- **Walking in.** After the room has been still for 3 minutes, movement makes him take a photo. If a person is in it, he says hello. He won't greet again for 10 minutes.
- **Leaning in.** Come close to his face and he reacts.

Chill mode turns both off. See [Privacy and cost](privacy-and-cost.md) for what happens to the photo.

## If you have WLED lights

He finds WLED lights on your Wi-Fi by himself and uses the names you gave them in the WLED app.

- "Turn the desk lamp blue."
- "All lights off."
- "Set the shelf to Fire 2012."

With no WLED lights he'll say he doesn't have any hooked up.

## If you have Spotify on this Mac

The Spotify desktop app must be installed.

- "Play some jazz." He searches for a playlist, then plays it.
- "Volume 40."

## If you have a Spotify connector

With the Spotify connector (see [Connectors](connectors.md)) he talks to Spotify directly, which is faster than the desktop-app route above and does more:

- "Play some ska." "Play Three Feet High and Rising." He finds and plays it.
- "What's this song?"
- "Add this to my Chill playlist." "Make a playlist called Road Trip."
- "Like this one."
- "What have I been listening to?"

When he starts something or tells you what's playing, the album cover shows on his screen for a few seconds (bot firmware 3.7.34 or newer). Playing and skipping need Spotify Premium.

## Kevin's lab only

Keyboard lighting effects, "what's playing?", and pause, skip and previous need vizMac, an app that only runs in Kevin's lab. Without it he'll say he doesn't have that hooked up.

## Limits worth knowing

- **12 seconds per question.** He stops listening after that.
- **30 seconds per answer.** Longer answers are cut off, so ask for the short version.
- **He forgets the conversation after 5 quiet minutes.** Things you asked him to remember are kept.
- **60 remembered facts.** After that the oldest drop off.
- **Two web searches per answer.**
- **Four connector look-ups per answer**, and 15 seconds of waiting on them.
- **One voice.**
- **A daily spending limit.** If he reaches it he says so and is back the next day.
- **Your Mac must be awake.** He can't think while it sleeps.

## What he will never do

He can't switch himself off, change his Wi-Fi, rename himself or update his own firmware. Those stay with you.
