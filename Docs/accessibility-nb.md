# Tilgjengelighetserklæring

Denne appen skal kunne brukes med skjermleser, med bare tastatur og med nedsatt syn. Her står det hva som er sjekket, og hva vi vet at ikke virker ennå.

## Dette tilbyr den

- **Skjermlesere:** virker gjennom AT-SPI. Kontroller har navn og roller, og grafer og målere oppgir verdien sin som en beskrivelse.
- **Tastatur:** alt i hovedvinduet kan nås uten mus. Tab flytter mellom kontroller, piltastene flytter mellom faner, Ctrl+Tab bytter side, Enter og mellomrom aktiverer, Esc lukker, Alt+mellomrom åpner vindusmenyen, og Alt med den understrekede bokstaven åpner menyer. Kontrollen som har fokus, får en prikket ramme som er tykkere enn vanlig.
- **Kontrast:** tekst oppfyller WCAG AA (4,5:1) i alle de ti fargeskjemaene, og det sjekkes av en automatisk test. Et skjema med høy kontrast følger med.
- **Tekststørrelse:** 100 % til 200 % i innstillingene. Valget huskes, og vinduet vokser så alt får plass.
- **Bevegelse:** appen har ingen animasjoner, så det er ingenting for innstillinger for redusert bevegelse å slå av. Avspilling i ferdsskriveren går bare når du starter den selv.
- **Farge og lyd:** ingenting formidles bare med farge, og appen lager ingen lyd. Effektivitetskjerner er merket P eller E i navnet, og grafer med to linjer sier hvilken linje som er hva ved siden av grafen.
- **Tid:** ingenting har tidsfrist.

## Kjente begrensninger

- Knappene for å minimere, maksimere og lukke i tittellinjen kan ikke nås med Tab. Bruk Alt+mellomrom eller Alt+F4 i stedet.
- Klikk i kartet under Diskplass og spoling med mus i ferdsskriverens paneler har ingen egen tastaturvei. Listen ved siden av kartet har de samme elementene, og posisjonsglidebryteren gjør det samme som spoling.
- Flytting og størrelse på miniviseren er opp til skrivebordet, og den har ikke menylinje. Den lukkes med Esc, og menyen åpnes med menytasten eller Shift+F10.
- Fullskjermvisningen av belastning er til å se på. Tallene leses ikke opp etter hvert som de endrer seg.

## Testing

Sjekket med automatiske tester, som går gjennom tilgjengelighetstreet på hver side og krever at hver kontroll har rolle og navn, og som sjekker kontrasten i hvert skjema. Den er ennå ikke kjørt med en virkelig skjermleser eller testet av folk som er avhengige av en, så virkelig bruk kan avdekke problemer disse sjekkene ikke finner.

## Si fra

Hvis noe hindrer deg i å bruke appen, skriv gjerne til oss. Tilbakemeldinger tas imot skriftlig gjennom prosjektets saksliste.
