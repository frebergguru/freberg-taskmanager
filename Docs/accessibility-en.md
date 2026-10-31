# Accessibility statement

This app aims to be usable with a screen reader, with the keyboard alone, and with low vision. This statement says what has been checked and what is known not to work yet.

## What it offers

- **Screen readers:** built to work through AT-SPI, though not yet confirmed with a real screen reader (see Testing). Controls have names and roles, and graphs and meters give their current value as a description.
- **Keyboard:** everything in the main window can be reached without a mouse. Tab moves between controls, arrow keys move between tabs, Ctrl+Tab changes page, Enter and Space activate, Esc closes, Alt+Space opens the window menu, and Alt with the underlined letter opens menus. The focused control has a dotted outline, drawn thicker than usual.
- **Contrast:** text meets WCAG AA (4.5:1) in all eleven themes, in both their light and dark versions and at every colour saturation setting, checked by an automated test. A High Contrast theme is included, in light and dark.
- **Text size:** 100% to 200% in the settings. It is remembered, the window grows to fit, and the popout follows it.
- **Motion:** the app has no animation, so there is nothing for reduced-motion settings to turn off. Replay in the Flight Recorder only runs when you start it.
- **Colour and sound:** nothing is told by colour alone, and the app makes no sound. Efficiency cores are marked P or E in their names. Graphs with two lines draw the second one dashed and name each line beside the graph, and folders in the disk map end with a slash.
- **Timing:** nothing has a time limit.

## Known limits

- Lists other than Processes can only be sorted with the mouse. On Processes, the Columns button chooses the columns and the sort order from the keyboard.
- The title bar's minimize, maximize and close buttons aren't keyboard stops. Use Alt+Space or Alt+F4 instead.
- Scrubbing with the mouse in the Flight Recorder panels has no direct keyboard route. The position slider does the same.
- The popout's moving and resizing are left to the desktop, and it has no menu bar. It closes with Esc, and its menu opens with the Menu key or Shift+F10.
- The full-screen load view is for looking at. Its figures are not spoken as they change.

## Testing

Checked with automated tests, which walk every page's accessibility tree and require each control of the kinds they know (buttons, tabs, lists, fields and graphs) to have a name, and which check contrast in every scheme. It has not yet been run with a real screen reader, or tested by people who rely on one, so real use may show problems that these checks can't.

## Tell us

If something stops you from using the app, please write to us. Feedback is welcome in writing through the project's issue tracker.
