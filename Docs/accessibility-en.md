# Accessibility statement

This app aims to be usable with a screen reader, with the keyboard alone, and with low vision. This statement says what has been checked and what is known not to work yet.

## What it offers

- **Screen readers:** works through AT-SPI. Controls have names and roles, and graphs and meters give their current value as a description.
- **Keyboard:** everything in the main window can be reached without a mouse. Tab moves between controls, arrow keys move between tabs, Ctrl+Tab changes page, Enter and Space activate, Esc closes, Alt+Space opens the window menu, and Alt with the underlined letter opens menus. The focused control has a dotted outline, drawn thicker than usual.
- **Contrast:** text meets WCAG AA (4.5:1) in all ten colour schemes, checked by an automated test. A High Contrast scheme is included.
- **Text size:** 100% to 200% in the settings. It is remembered, and the window grows to fit.
- **Motion:** the app has no animation, so there is nothing for reduced-motion settings to turn off. Replay in the Flight Recorder only runs when you start it.
- **Colour and sound:** nothing is told by colour alone, and the app makes no sound. Efficiency cores are marked P or E in their names, and graphs with two lines name each line beside the graph.
- **Timing:** nothing has a time limit.

## Known limits

- The title bar's minimize, maximize and close buttons aren't keyboard stops. Use Alt+Space or Alt+F4 instead.
- Clicking in the Disk Space map and scrubbing with the mouse in the Flight Recorder panels have no direct keyboard route. The list beside the map holds the same items, and the position slider does the same as scrubbing.
- The popout's moving and resizing are left to the desktop, and it has no menu bar. It closes with Esc, and its menu opens with the Menu key or Shift+F10.
- The full-screen load view is for looking at. Its figures are not spoken as they change.

## Testing

Checked with automated tests, which walk every page's accessibility tree and require each control to have a role and a name, and which check contrast in every scheme. It has not yet been run with a real screen reader, or tested by people who rely on one, so real use may show problems that these checks can't.

## Tell us

If something stops you from using the app, please write to us. Feedback is welcome in writing through the project's issue tracker.
