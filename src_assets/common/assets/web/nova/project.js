/**
 * @file Project links used across the web UI. Change them here when the repository moves.
 */

const REPO = 'F-e-n-y-x/nova-host'
const REPO_URL = `https://github.com/${REPO}`

export const project = {
  name: 'Nova',
  repoUrl: REPO_URL,
  releasesApi: `https://api.github.com/repos/${REPO}/releases`,
  releasesUrl: `${REPO_URL}/releases`,
  docsUrl: `${REPO_URL}/tree/HEAD/docs`,
  issuesUrl: `${REPO_URL}/issues`,
  licenseUrl: `${REPO_URL}/blob/HEAD/LICENSE`,
  noticeUrl: `${REPO_URL}/blob/HEAD/NOTICE`,
}

/** Projects Nova is built on, credited on the Help page. */
export const upstreamProjects = [
  { name: 'Sunshine', url: 'https://github.com/LizardByte/Sunshine' },
  { name: 'Zenith', url: 'https://github.com/jacksonpate/zenith' },
]
