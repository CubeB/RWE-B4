/**
 * What `electron` is, to a story.
 *
 * Storybook renders components in a browser, where there is no Electron and
 * so no `webUtils`; Wizard's directory picker asks it for the path of the
 * chosen directory. Returning the name keeps the story interactive and says
 * plainly that it is not the real path -- a component sandbox has no business
 * knowing one.
 */
export const webUtils = {
  getPathForFile: (file: File) => file.name,
};
